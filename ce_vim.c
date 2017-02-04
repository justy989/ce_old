#include "ce_vim.h"

#include <assert.h>
#include <ctype.h>

Point_t* vim_mark_find(VimMarkNode_t* head, char mark_char)
{
     VimMarkNode_t* itr = head;
     while(itr != NULL){
          if(itr->reg_char == mark_char) return &itr->location;
          itr = itr->next;
     }
     return NULL;
}

void vim_mark_add(VimMarkNode_t** head, char mark_char, const Point_t* location)
{
     Point_t* mark_location = vim_mark_find(*head, mark_char);
     if(!mark_location){
          VimMarkNode_t* new_mark = malloc(sizeof(**head));
          new_mark->reg_char = mark_char;
          new_mark->next = *head;
          *head = new_mark;
          mark_location = &new_mark->location;
     }
     *mark_location = *location;
}

void vim_marks_free(VimMarkNode_t** head)
{
     VimMarkNode_t* itr = *head;
     while(itr){
          VimMarkNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     *head = NULL;
}

VimYankNode_t* vim_yank_find(VimYankNode_t* head, char reg_char)
{
     VimYankNode_t* itr = head;
     while(itr != NULL){
          if(itr->reg_char == reg_char) return itr;
          itr = itr->next;
     }
     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void vim_yank_add(VimYankNode_t** head, char reg_char, const char* yank_text, VimYankMode_t mode)
{
     VimYankNode_t* node = vim_yank_find(*head, reg_char);
     if(node != NULL){
          free((void*)node->text);
     }else{
          VimYankNode_t* new_yank = malloc(sizeof(*new_yank));
          new_yank->reg_char = reg_char;
          new_yank->next = *head;
          node = new_yank;
          *head = new_yank;
     }

     node->text = yank_text;
     node->mode = mode;
}

void vim_yanks_free(VimYankNode_t** head)
{
     VimYankNode_t* itr = *head;
     while(itr){
          VimYankNode_t* tmp = itr;
          itr = itr->next;

          free((void*)(tmp->text));
          free(tmp);
     }

     *head = NULL;
}

void vim_macros_free(VimMacroNode_t** head)
{
     VimMacroNode_t* itr = *head;

     while(itr){
          VimMacroNode_t* tmp = itr;
          itr = itr->next;
          free(tmp);
     }

     *head = NULL;
}

VimMacroNode_t* vim_macro_find(VimMacroNode_t* head, char reg)
{
     VimMacroNode_t* itr = head;

     while(itr != NULL){
          if(itr->reg == reg) return itr;
          itr = itr->next;
     }

     return NULL;
}

// for now the yanked string is user allocated. eventually will probably
// want to change this interface so that everything is hidden
void vim_macro_add(VimMacroNode_t** head, char reg, int* command)
{
     VimMacroNode_t* node = vim_macro_find(*head, reg);

     if(node != NULL){
          free((void*)node->command);
     }else{
          VimMacroNode_t* new_yank = malloc(sizeof(*new_yank));
          new_yank->reg = reg;
          new_yank->next = *head;
          node = new_yank;
          *head = new_yank;
     }

     node->command = command;
}

void vim_macro_commits_free(VimMacroCommitNode_t** macro_commit)
{
     VimMacroCommitNode_t* itr = *macro_commit;
     while(itr){
          VimMacroCommitNode_t* tmp = itr;
          itr = itr->next;
          ce_keys_free(&tmp->command_copy);
          free(tmp);
     }

     *macro_commit = NULL;
}

void vim_macro_commits_init(VimMacroCommitNode_t** macro_commit)
{
     if(*macro_commit){
          vim_macro_commits_free(macro_commit);
     }

     *macro_commit = calloc(1, sizeof(**macro_commit));
}

void vim_macro_commit_push(VimMacroCommitNode_t** macro_commit, KeyNode_t* last_command_begin, bool chain)
{
     if(*macro_commit && (*macro_commit)->command_copy){
          vim_macro_commits_free(&(*macro_commit)->next);
     }

     VimMacroCommitNode_t* new_commit = calloc(1, sizeof(*new_commit));
     if(!new_commit) return;

     new_commit->prev = *macro_commit;
     (*macro_commit)->next = new_commit;

     (*macro_commit)->command_begin = last_command_begin;
     (*macro_commit)->chain = chain;

     *macro_commit = new_commit;
}

void vim_macro_commits_dump(const VimMacroCommitNode_t* macro_commit)
{
     const VimMacroCommitNode_t* itr = macro_commit;
     while(itr){
          ce_message("key: %c, chain: %d", itr->command_begin ? itr->command_begin->key : '0', itr->chain);
          KeyNode_t* copy_itr = itr->command_copy;
          while(copy_itr){
               ce_message("  %c", copy_itr->key);
               copy_itr = copy_itr->next;
          }
          itr = itr->next;
     }
}

static bool line_is_all_whitespace(Buffer_t* buffer, int64_t line)
{
     if(line < 0 || line >= buffer->line_count) return false;

     const char* line_itr = buffer->lines[line];
     if(!*line_itr) return false;

     bool all_whitespace = true;
     while(*line_itr){
          if(!isspace(*line_itr)) all_whitespace = false;
          line_itr++;
     }

     return all_whitespace;
}

VimKeyHandlerResult_t vim_key_handler(int key, VimState_t* vim_state, Buffer_t* buffer, Point_t* cursor,
                                      BufferCommitNode_t** commit_tail, VimBufferState_t* vim_buffer_state,
                                      bool repeating)
{
     char recording_macro = vim_state->recording_macro;
     VimMode_t vim_mode = vim_state->mode;

     VimKeyHandlerResult_t result;
     result.type = VKH_UNHANDLED_KEY;

     switch(vim_state->mode){
     default:
          assert(!"vim mode not handled");
          return result;
     case VM_INSERT:
     {
          Point_t insert_start = *cursor;
          Point_t undo_cursor = insert_start;

          if(vim_state->insert_start.x >= 0){
               undo_cursor = vim_state->insert_start;
               vim_state->insert_start = (Point_t){-1, -1};
          }

          switch(key){
          default:
               if(isprint(key)){
                    if(ce_insert_char(buffer, *cursor, key)){
                         ce_keys_push(&vim_state->command_head, key);
                         cursor->x++;
                         ce_commit_insert_char(commit_tail, insert_start, undo_cursor, *cursor, key, BCC_KEEP_GOING);
                    }
               }else{
                    return result;
               }
               break;
          case '#':
          {
               if(buffer->type == BFT_C && cursor->y < buffer->line_count){
                    bool all_whitespace = true;

                    for(int64_t i = 0; i < cursor->x; ++i){
                         if(ce_get_char_raw(buffer, (Point_t){i, cursor->y}) != ' '){
                              all_whitespace = false;
                              break;
                         }
                    }

                    if(all_whitespace && cursor->x > 0){
                         Point_t start = {0, cursor->y};
                         Point_t end = {cursor->x - 1, cursor->y};
                         char* removed_str = ce_dupe_string(buffer, start, end);
                         if(ce_remove_string(buffer, start, cursor->x)){
                              ce_commit_remove_string(commit_tail, start, *cursor, start, removed_str, BCC_KEEP_GOING);
                              *cursor = start;
                              insert_start = start;
                              undo_cursor = insert_start;
                         }else{
                              free(removed_str);
                         }
                    }
               }

               if(ce_insert_char(buffer, *cursor, key)){
                    ce_keys_push(&vim_state->command_head, key);
                    cursor->x++;
                    ce_commit_insert_char(commit_tail, insert_start, undo_cursor, *cursor, key, BCC_KEEP_GOING);
               }
          } break;
          case KEY_ESCAPE:
          {
               if(!repeating){
                    int* built_command = ce_keys_get_string(vim_state->command_head);
                    if(vim_state->last_insert_command) free(vim_state->last_insert_command);
                    vim_state->last_insert_command = built_command;
               }

               ce_keys_free(&vim_state->command_head);

               if(line_is_all_whitespace(buffer, cursor->y)){
                    Point_t remove_loc = {0, cursor->y};
                    int64_t remove_len = strlen(buffer->lines[cursor->y]);
                    char* remove_string = ce_dupe_string(buffer, remove_loc, (Point_t){remove_len - 1, remove_loc.y});

                    if(ce_remove_string(buffer, remove_loc, remove_len)){
                         ce_commit_remove_string(commit_tail, remove_loc, *cursor, remove_loc, remove_string, BCC_KEEP_GOING);
                    }else{
                         free(remove_string);
                    }
               }

               vim_enter_normal_mode(vim_state);
               ce_clamp_cursor(buffer, cursor);

               if(*commit_tail && !vim_state->playing_macro){
                    (*commit_tail)->commit.chain = BCC_STOP;
               }
          } break;
          case KEY_ENTER:
          {
               key = NEWLINE;

               Point_t save_cursor = *cursor;
               int64_t indent_len = ce_get_indentation_for_line(buffer, *cursor, strlen(TAB_STRING));

               if(ce_insert_char(buffer, *cursor, key)){
                    cursor->y++;
                    cursor->x = 0;

                    ce_commit_insert_char(commit_tail, insert_start, undo_cursor, *cursor, key, BCC_KEEP_GOING);
                    ce_keys_push(&vim_state->command_head, KEY_ENTER);

                    // indent if necessary
                    if(indent_len > 0){
                         char* indent = malloc(indent_len + 1);
                         memset(indent, ' ', indent_len);
                         indent[indent_len] = '\0';

                         if(ce_insert_string(buffer, *cursor, indent)){
                              Point_t pre_insert = *cursor;
                              cursor->x += indent_len;
                              ce_commit_insert_string(commit_tail, pre_insert, pre_insert, *cursor, indent, BCC_KEEP_GOING);
                         }
                    }

                    if(line_is_all_whitespace(buffer, save_cursor.y)){
                         Point_t remove_loc = {0, save_cursor.y};
                         int64_t remove_len = strlen(buffer->lines[save_cursor.y]);
                         char* remove_string = ce_dupe_string(buffer, remove_loc, (Point_t){remove_len - 1, remove_loc.y});

                         if(ce_remove_string(buffer, remove_loc, remove_len)){
                              ce_commit_remove_string(commit_tail, remove_loc, *cursor, remove_loc, remove_string, BCC_KEEP_GOING);
                         }else{
                              free(remove_string);
                         }
                    }
               }
          } break;
          case KEY_BACKSPACE:
          {
               Point_t before_cursor = {cursor->x - 1, cursor->y};
               if(before_cursor.x < 0){
                    if(!before_cursor.y) break; // don't do anything if the user tries to backspace at 0, 0
                    before_cursor.x = strlen(buffer->lines[cursor->y - 1]);
                    before_cursor.y--;
               }

               char ch;
               if(ce_get_char(buffer, before_cursor, &ch)){
                    if(ce_remove_char(buffer, before_cursor)){
                         ce_commit_remove_char(commit_tail, before_cursor, *cursor, before_cursor, ch, BCC_KEEP_GOING);
                         *cursor = before_cursor;
                         ce_keys_push(&vim_state->command_head, key);
                    }
               }
          } break;
          case KEY_TAB:
          {
               if(ce_insert_string(buffer, insert_start, TAB_STRING)){
                    ce_move_cursor(buffer, cursor, (Point_t){strlen(TAB_STRING) - 1, 0});
                    cursor->x++; // we want to be after the tabs
                    ce_commit_insert_string(commit_tail, insert_start, undo_cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
                    ce_keys_push(&vim_state->command_head, key);
               }
          } break;
          case KEY_UP:
          case KEY_DOWN:
          {
               ce_move_cursor(buffer, cursor, (Point_t){0, (key == KEY_DOWN) ? 1 : -1});
               if(*commit_tail && !vim_state->playing_macro){
                    (*commit_tail)->commit.chain = BCC_STOP;
               }

               int* built_command = ce_keys_get_string(vim_state->command_head);
               if(vim_state->last_insert_command) free(vim_state->last_insert_command);
               vim_state->last_insert_command = built_command;
               ce_keys_free(&vim_state->command_head);
          } break;
          case KEY_LEFT:
          case KEY_RIGHT:
          {
               cursor->x += key == KEY_RIGHT? 1 : -1;
               if(cursor->x > (int64_t) strlen(buffer->lines[cursor->y])) cursor->x--;
               if(cursor->x < 0) cursor->x++;
               if(*commit_tail && !vim_state->playing_macro){
                    (*commit_tail)->commit.chain = BCC_STOP;
               }

               int* built_command = ce_keys_get_string(vim_state->command_head);
               if(vim_state->last_insert_command) free(vim_state->last_insert_command);
               vim_state->last_insert_command = built_command;
               ce_keys_free(&vim_state->command_head);
          } break;
          case '}':
          {
               if(ce_insert_char(buffer, *cursor, key)){
                    ce_keys_push(&vim_state->command_head, key);
                    Point_t next_cursor = {cursor->x + 1, cursor->y};
                    ce_commit_insert_char(commit_tail, *cursor, *cursor, next_cursor, key, BCC_KEEP_GOING);

                    bool do_indentation = true;
                    for(int i = 0; i < cursor->x; i++){
                         char blank_c;
                         Point_t itr = {i, cursor->y};
                         if(ce_get_char(buffer, itr, &blank_c)){
                              // we only change the indentation if everything to the left of the cursor is blank
                              if(!isblank(blank_c)){
                                   do_indentation = false;
                                   break;
                              }
                         }
                         else assert(0);
                    }

                    if(do_indentation){
                         Point_t match = *cursor;

                         char matchee;
                         if(!ce_get_char(buffer, match, &matchee)) break;

                         if(ce_move_cursor_to_matching_pair(buffer, &match, matchee) && match.y != cursor->y){
                              // get the match's sbol (that's the indentation we're matching)
                              Point_t sbol_match = {0, match.y};
                              ce_move_cursor_to_soft_beginning_of_line(buffer, &sbol_match);

                              if(cursor->x < sbol_match.x){
                                   // we are adding spaces
                                   int64_t n_spaces = sbol_match.x - cursor->x;
                                   for(int64_t i = 0; i < n_spaces; i++){
                                        Point_t itr = {cursor->x + i, cursor->y};
                                        if(!ce_insert_char(buffer, itr, ' ')) assert(0);
                                        ce_commit_insert_char(commit_tail, itr, *cursor, itr, ' ', BCC_KEEP_GOING);
                                   }
                                   cursor->x = sbol_match.x;
                              }else{
                                   int64_t n_deletes = CE_MIN((int64_t) strlen(TAB_STRING), cursor->x - sbol_match.x);

                                   bool can_unindent = true;
                                   for(Point_t iter = {0, cursor->y}; ce_point_on_buffer(buffer, iter) && iter.x < n_deletes; iter.x++){
                                        if(!isblank(ce_get_char_raw(buffer, iter))){
                                             can_unindent = false;
                                             break;
                                        }
                                   }

                                   if(can_unindent){
                                        Point_t end_of_delete = *cursor;
                                        end_of_delete.x--;
                                        if(end_of_delete.x < 0) end_of_delete.x = 0;
                                        cursor->x -= n_deletes;
                                        char* duped_str = ce_dupe_string(buffer, *cursor, end_of_delete);
                                        if(ce_remove_string(buffer, *cursor, n_deletes)){
                                             ce_commit_remove_string(commit_tail, *cursor, end_of_delete, *cursor, duped_str, BCC_KEEP_GOING);
                                        }
                                   }
                              }
                         }
                    }

                    cursor->x++;
               }
          } break;
          }

          if(recording_macro && recording_macro == vim_state->recording_macro){
               ce_keys_push(&vim_state->record_macro_head, key);

               if(vim_state->macro_commit_current->next){
                    vim_macro_commits_free(&vim_state->macro_commit_current->next);
                    ce_keys_free(&vim_state->macro_commit_current->command_copy);
               }

               ce_keys_push(&vim_state->macro_commit_current->command_copy, key);

               // when we exit insert mode, track the last macro action
               if(vim_mode == VM_INSERT && vim_state->mode == VM_NORMAL){
                    vim_macro_commit_push(&vim_state->macro_commit_current, vim_state->last_macro_command_begin, false);
               }
          }
     } break;
     case VM_VISUAL_RANGE:
     case VM_VISUAL_LINE:
         if(key == KEY_ESCAPE){
               vim_enter_normal_mode(vim_state);
               break;
         }else if(key == 'v'){
               vim_enter_visual_range_mode(vim_state, *cursor);
               break;
         }else if(key == 'V'){
               vim_enter_visual_line_mode(vim_state, *cursor);
               break;
          }
     case VM_NORMAL:
     {
          ce_keys_push(&vim_state->command_head, key);
          int* built_command = ce_keys_get_string(vim_state->command_head);

          VimAction_t vim_action;
          VimCommandState_t command_state = vim_action_from_string(built_command, &vim_action, vim_state->mode, buffer,
                                                                   cursor, &vim_state->visual_start, &vim_state->find_char_state,
                                                                   vim_state->recording_macro);
          free(built_command);

          switch(command_state){
          default:
          case VCS_INVALID:
               // allow command to be cleared
               ce_keys_free(&vim_state->command_head);
               return result; // did not handle key
          case VCS_CONTINUE:
               if(recording_macro && recording_macro == vim_state->recording_macro){
                    // TODO: compress
                    ce_keys_push(&vim_state->record_macro_head, key);

                    if(vim_state->macro_commit_current->next){
                         vim_macro_commits_free(&vim_state->macro_commit_current->next);
                         ce_keys_free(&vim_state->macro_commit_current->command_copy);
                    }

                    ce_keys_push(&vim_state->macro_commit_current->command_copy, key);

                    if(!vim_state->command_head->next){
                         KeyNode_t* itr = vim_state->record_macro_head;
                         while(itr->next) {
                              itr = itr->next;
                         }
                         vim_state->last_macro_command_begin = itr;
                    }
               }
               break;
          case VCS_COMPLETE:
          {
               VimMode_t original_mode = vim_state->mode;
               bool successful_action = vim_action_apply(&vim_action, buffer, cursor, vim_state, commit_tail,
                                                         vim_buffer_state);

               if(vim_state->mode != original_mode){
                    switch(vim_state->mode){
                    default:
                         break;
                    case VM_INSERT:
                         vim_enter_insert_mode(vim_state, buffer);
                         break;
                    case VM_NORMAL:
                         vim_enter_normal_mode(vim_state);
                         break;
                    case VM_VISUAL_RANGE:
                         vim_enter_visual_range_mode(vim_state, *cursor);
                         break;
                    case VM_VISUAL_LINE:
                         vim_enter_visual_line_mode(vim_state, *cursor);
                         break;
                    }
               }

               if(vim_action.change.type != VCT_MOTION || vim_action.end_in_vim_mode == VM_INSERT){
                    if(!vim_state->playing_macro && successful_action){
                         vim_state->last_action = vim_action;

                         if(!vim_state->recording_macro && vim_action.change.type == VCT_RECORD_MACRO){
                              vim_state->last_action.change.type = VCT_PLAY_MACRO;
                              vim_state->last_action.change.reg = recording_macro;
                         }
                    }

                    // always use the cursor as the start of the visual selection, unless the action was an indent/unindent
                    if((vim_state->last_action.motion.type == VMT_VISUAL_RANGE ||
                        vim_state->last_action.motion.type == VMT_VISUAL_LINE) &&
                       (vim_state->last_action.change.type != VCT_INDENT &&
                        vim_state->last_action.change.type != VCT_UNINDENT)){
                         vim_state->last_action.motion.visual_start_after = true;
                         vim_state->last_action.motion.visual_length = labs(vim_state->last_action.motion.visual_length);
                    }
               }

               if(recording_macro && recording_macro == vim_state->recording_macro){
                    assert(vim_state->macro_commit_current);

                    ce_keys_push(&vim_state->record_macro_head, key);

                    if(!vim_state->command_head->next){
                         KeyNode_t* itr = vim_state->record_macro_head;
                         while(itr->next) itr = itr->next;
                         vim_state->last_macro_command_begin = itr;
                    }

                    if(vim_state->macro_commit_current->next){
                         vim_macro_commits_free(&vim_state->macro_commit_current->next);
                         ce_keys_free(&vim_state->macro_commit_current->command_copy);
                    }

                    ce_keys_push(&vim_state->macro_commit_current->command_copy, key);

                    // tag this command as a macro commit
                    if(vim_state->mode != VM_INSERT){
                         vim_macro_commit_push(&vim_state->macro_commit_current, vim_state->last_macro_command_begin, vim_action.change.type == VCT_MOTION);
                    }
               }

               ce_keys_free(&vim_state->command_head);

               result.type = successful_action ? VKH_COMPLETED_ACTION_SUCCESS : VKH_COMPLETED_ACTION_FAILURE;
               result.completed_action = vim_action;

               return result;
          } break;
          }
     } break;
     }

     result.type = VKH_HANDLED_KEY;

     return result;
}

static int itoi(int* str)
{
     int value = 0;

     while(*str){
          if(*str < 0 || *str > 255 || !isdigit(*str)) return 0;
          value = value * 10 + (*str - '0');
          str++;
     }

     return value;
}

VimCommandState_t vim_action_from_string(const int* string, VimAction_t* action, VimMode_t vim_mode,
                                         Buffer_t* buffer, Point_t* cursor, Point_t* visual_start,
                                         VimFindCharState_t* find_char_state, bool recording_macro)
{
     int tmp[BUFSIZ];
     bool visual_mode = false;
     bool get_motion = true;
     VimAction_t built_action = {};

     built_action.multiplier = 1;
     built_action.motion.multiplier = 1;

     // get multiplier if there is one
     const int* itr = string;
     while(*itr && *itr >= 0 && *itr <= 255 && isdigit(*itr)) itr++;
     if(itr != string){
          int64_t len = itr - string;
          memcpy(tmp, string, len * sizeof(*tmp));
          tmp[len] = 0;
          built_action.multiplier = itoi(tmp);

          if(built_action.multiplier == 0){
               // it's actually just a motion to move to the beginning of the line!
               built_action.end_in_vim_mode = vim_mode;
               built_action.multiplier = 1;
               built_action.change.type = VCT_MOTION;
               built_action.motion.type = VMT_BEGINNING_OF_LINE_HARD;
               *action = built_action;
               return VCS_COMPLETE;
          }
     }

     // set motions early if visual mode, allowing them to be overriden by any action that wants to
     if(vim_mode == VM_VISUAL_RANGE){
          visual_mode = true;
          get_motion = false;
          built_action.motion.type = VMT_VISUAL_RANGE;
          built_action.motion.visual_length = ce_compute_length(buffer, *visual_start, *cursor) - 1;
          built_action.motion.visual_start_after = ce_point_after(*visual_start, *cursor);
     }else if(vim_mode == VM_VISUAL_LINE){
          visual_mode = true;
          get_motion = false;
          built_action.motion.type = VMT_VISUAL_LINE;
          built_action.motion.visual_lines = visual_start->y - cursor->y;
          built_action.motion.visual_start_after = ce_point_after(*visual_start, *cursor);
     }

     int change_char = *itr;

     // check for yank registers
     if(*itr == '"'){
          itr++;

          if(*itr == '\0'){
               return VCS_CONTINUE;
          } else if(!isprint(*itr) || *itr == '?'){
               return VCS_INVALID;
          }

          built_action.change.reg = *itr;
          itr++;
     }

     // get the change
     change_char = *itr;
     switch(*itr){
     default:
          built_action.end_in_vim_mode = vim_mode;
          built_action.change.type = VCT_MOTION;
          itr--;
          if(visual_mode) get_motion = true; // if we are just executing a motion, use override the built motion
          break;
     case 'd':
          built_action.change.type = VCT_DELETE;
          built_action.yank = true;
          break;
     case 'D':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.yank = true;
          get_motion = false;
          break;
     case 'c':
          built_action.change.type = VCT_DELETE;
          built_action.end_in_vim_mode = VM_INSERT;
          built_action.yank = true;
          break;
     case 'C':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.end_in_vim_mode = VM_INSERT;
          built_action.yank = true;
          get_motion = false;
          break;
     case 'a':
          if(vim_mode == VM_VISUAL_RANGE) { // wait for aw in visual range mode
               built_action.change.type = VCT_MOTION;
               built_action.end_in_vim_mode = vim_mode;
               itr--; // back up so this counts as a motion
               get_motion = true;
               break;
          }
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_RIGHT;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'A':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_END_OF_LINE_PASSED;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 's':
          built_action.change.type = VCT_DELETE;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'S':
          built_action.change.type = VCT_DELETE;
          built_action.motion.type = VMT_LINE_SOFT;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'i':
          if(vim_mode == VM_VISUAL_RANGE) { // wait for iw in visual range mode
               built_action.change.type = VCT_MOTION;
               built_action.end_in_vim_mode = vim_mode;
               itr--; // back up so this counts as a motion
               get_motion = true;
               break;
          }
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'v':
          built_action.end_in_vim_mode = VM_VISUAL_RANGE;
          get_motion = false;
          break;
     case 'V':
          built_action.end_in_vim_mode = VM_VISUAL_LINE;
          get_motion = false;
          break;
     case 'I':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_BEGINNING_OF_LINE_SOFT;
          built_action.end_in_vim_mode = VM_INSERT;
          get_motion = false;
          break;
     case 'x':
          built_action.change.type = VCT_DELETE;
          get_motion = false;
          if(visual_mode) get_motion = false;
          break;
     case 'r':
          built_action.change.type = VCT_CHANGE_CHAR;
          built_action.change.change_char = *(++itr);
          if(!built_action.change.change_char){
               return VCS_CONTINUE;
          }
          if(!isprint(built_action.change.change_char)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     case 'g':
     {
          built_action.end_in_vim_mode = vim_mode;
          char next_ch = *(itr + 1);
          if(next_ch == 'c'){
               built_action.change.type = VCT_COMMENT;
               built_action.end_in_vim_mode = VM_NORMAL;
          }else if(next_ch == 'u'){
               built_action.change.type = VCT_UNCOMMENT;
               built_action.end_in_vim_mode = VM_NORMAL;
          }else if(next_ch == 0){
               return VCS_CONTINUE;
          }else{
               built_action.change.type = VCT_MOTION;
               if(visual_mode) get_motion = true;
          }
     } break;
     case 'p':
          built_action.change.type = VCT_PASTE_AFTER;
          get_motion = false;
          break;
     case 'P':
          built_action.change.type = VCT_PASTE_BEFORE;
          get_motion = false;
          break;
     case 'y':
          built_action.change.type = VCT_YANK;
          break;
     case 'Y':
          built_action.change.type = VCT_YANK;
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          get_motion = false;
          break;
     case '>':
          built_action.change.type = VCT_INDENT;
          built_action.end_in_vim_mode = vim_mode;
          break;
     case '<':
          built_action.change.type = VCT_UNINDENT;
          built_action.end_in_vim_mode = vim_mode;
          break;
     case '~':
          built_action.change.type = VCT_FLIP_CASE;
          break;
     case ';':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = find_char_state->motion_type;
          get_motion = false;
          break;
     case ',':
          built_action.change.type = VCT_MOTION;

          // reverse the motion
          switch(find_char_state->motion_type){
          default:
               break;
          case VMT_FIND_NEXT_MATCHING_CHAR:
               built_action.motion.type = VMT_FIND_PREV_MATCHING_CHAR;
               break;
          case VMT_FIND_PREV_MATCHING_CHAR:
               built_action.motion.type = VMT_FIND_NEXT_MATCHING_CHAR;
               break;
          case VMT_TO_NEXT_MATCHING_CHAR:
               built_action.motion.type = VMT_TO_PREV_MATCHING_CHAR;
               break;
          case VMT_TO_PREV_MATCHING_CHAR:
               built_action.motion.type = VMT_TO_NEXT_MATCHING_CHAR;
               break;
          }
          get_motion = false;
          break;
     case 'J':
          built_action.motion.type = VMT_END_OF_LINE_HARD;
          built_action.change.type = VCT_JOIN_LINE;
          get_motion = false;
          break;
     case 'O':
          if(visual_mode){
               built_action.motion.type = VMT_VISUAL_SWAP_WITH_CURSOR;
               built_action.end_in_vim_mode = vim_mode;
          }else{
               built_action.change.type = VCT_OPEN_ABOVE;
               built_action.end_in_vim_mode = VM_INSERT;
          }
          get_motion = false;
          break;
     case 'o':
          if(visual_mode){
               built_action.motion.type = VMT_VISUAL_SWAP_WITH_CURSOR;
               built_action.end_in_vim_mode = vim_mode;
          }else{
               built_action.change.type = VCT_OPEN_BELOW;
               built_action.end_in_vim_mode = VM_INSERT;
          }
          get_motion = false;
          break;
     case 'q':
          built_action.change.type = VCT_RECORD_MACRO;
          if(!recording_macro){
               built_action.change.reg = *(++itr);
               if(!built_action.change.reg){
                    return VCS_CONTINUE;
               }
               if(!isprint(built_action.change.reg)){
                    return VCS_INVALID;
               }
          }
          get_motion = false;
          break;
     case '@':
          built_action.change.type = VCT_PLAY_MACRO;
          built_action.change.reg = *(++itr);
          if(!built_action.change.reg){
               return VCS_CONTINUE;
          }
          if(!isprint(built_action.change.reg)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     case '%':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_MATCHING_PAIR;
          built_action.end_in_vim_mode = vim_mode;
          get_motion = false;
          break;
     case '*':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_SEARCH_WORD_UNDER_CURSOR;
          built_action.motion.search_direction = CE_DOWN;
          built_action.end_in_vim_mode = vim_mode;
          get_motion = false;
          break;
     case '#':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_SEARCH_WORD_UNDER_CURSOR;
          built_action.motion.search_direction = CE_UP;
          built_action.end_in_vim_mode = vim_mode;
          get_motion = false;
          break;
     case 'n':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_SEARCH;
          built_action.motion.search_direction = CE_DOWN;
          built_action.end_in_vim_mode = vim_mode;
          get_motion = false;
          break;
     case 'N':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_SEARCH;
          built_action.motion.search_direction = CE_UP;
          built_action.end_in_vim_mode = vim_mode;
          get_motion = false;
          break;
     case 'm':
          built_action.change.type = VCT_SET_MARK;
          built_action.change.reg = *(++itr);
          if(!built_action.change.reg){
               return VCS_CONTINUE;
          }
          if(!isprint(built_action.change.reg)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     case '\'':
          built_action.change.type = VCT_MOTION;
          built_action.motion.type = VMT_GOTO_MARK;
          built_action.motion.reg = *(++itr);
          if(!built_action.motion.reg){
               return VCS_CONTINUE;
          }
          if(!isprint(built_action.motion.reg)){
               return VCS_INVALID;
          }
          get_motion = false;
          break;
     }

     if(get_motion){
          // get the motion multiplier
          itr++;
          const int* start_itr = itr;
          while(*itr && isdigit(*itr)) itr++;
          if(itr != start_itr){
               int64_t len = itr - start_itr;
               memcpy(tmp, start_itr, len * sizeof(*tmp));
               tmp[len] = 0;
               built_action.motion.multiplier = itoi(tmp);

               if(built_action.motion.multiplier == 0){
                    built_action.motion.multiplier = 1;
                    itr--;
               }
          }

          // get the motion
          switch(*itr){
          default:
               return VCS_INVALID;
          case '\0':
               return VCS_CONTINUE;
          case 'h':
          case KEY_LEFT:
               built_action.motion.type = VMT_LEFT;
               break;
          case 'j':
          case KEY_DOWN:
               if(built_action.change.type == VCT_MOTION){
                    built_action.motion.type = VMT_DOWN;
               }else{
                    built_action.motion.type = VMT_LINE_DOWN;
               }
               break;
          case 'k':
          case KEY_UP:
               if(built_action.change.type == VCT_MOTION){
                    built_action.motion.type = VMT_UP;
               }else{
                    built_action.motion.type = VMT_LINE_UP;
               }
               break;
          case KEY_RIGHT:
          case 'l':
               built_action.motion.type = VMT_RIGHT;
               break;
          case 'w':
               built_action.motion.type = VMT_WORD_LITTLE;
               break;
          case 'W':
               built_action.motion.type = VMT_WORD_BIG;
               break;
          case 'b':
               built_action.motion.type = VMT_WORD_BEGINNING_LITTLE;
               break;
          case 'B':
               built_action.motion.type = VMT_WORD_BEGINNING_BIG;
               break;
          case 'e':
               built_action.motion.type = VMT_WORD_END_LITTLE;
               break;
          case 'E':
               built_action.motion.type = VMT_WORD_END_BIG;
               break;
          case 'f':
               built_action.motion.type = VMT_FIND_NEXT_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 'F':
               built_action.motion.type = VMT_FIND_PREV_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 't':
               built_action.motion.type = VMT_TO_NEXT_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case 'T':
               built_action.motion.type = VMT_TO_PREV_MATCHING_CHAR;
               built_action.motion.match_char = *(++itr);
               if(!built_action.motion.match_char) return VCS_CONTINUE;
               break;
          case '$':
               built_action.motion.type = VMT_END_OF_LINE_HARD;
               break;
          case '^':
               built_action.motion.type = VMT_BEGINNING_OF_LINE_SOFT;
               break;
          case '0':
               built_action.motion.type = VMT_BEGINNING_OF_LINE_HARD;
               break;
          case 'i': // inside
          {
               char ch = *(++itr);

               switch(ch){
               default:
                    return VCS_INVALID;
               case '\0':
                    return VCS_CONTINUE;
               case 'w':
                    built_action.motion.type = VMT_INSIDE_WORD_LITTLE;
                    break;
               case 'W':
                    built_action.motion.type = VMT_INSIDE_WORD_BIG;
                    break;
               case '"':
               case '\'':
               case ')':
               case '(':
               case '}':
               case '{':
               case '[':
               case ']':
                    built_action.motion.type = VMT_INSIDE_PAIR;
                    built_action.motion.inside_pair = ch;
                    break;
               }
          } break;
          case 'a': // around
          {
               char ch = *(++itr);

               switch(ch){
               default:
                    return VCS_INVALID;
               case '\0':
                    return VCS_CONTINUE;
               case 'w':
                    built_action.motion.type = VMT_AROUND_WORD_LITTLE;
                    break;
               case 'W':
                    built_action.motion.type = VMT_AROUND_WORD_BIG;
                    break;
               case '"':
               case '\'':
               case ')':
               case '(':
               case '}':
               case '{':
               case '[':
               case ']':
                    built_action.motion.type = VMT_AROUND_PAIR;
                    built_action.motion.around_pair = ch;
                    break;
               }
          } break;
          case 'g':
               if(change_char == 'g') {
                    built_action.motion.type = VMT_BEGINNING_OF_FILE;
               }else{
                    return VCS_INVALID;
               }
          break;
          case 'u':
               if(change_char == 'g') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
          break;
          case 'G':
               built_action.motion.type = VMT_END_OF_FILE;
               break;
          case 'c':
               if(change_char == 'c') {
                    built_action.motion.type = VMT_LINE_SOFT;
               }else if(change_char == 'g') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case 'd':
               if(change_char == 'd') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case 'y':
               if(change_char == 'y') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case '<':
               if(change_char == '<') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case '>':
               if(change_char == '>') {
                    built_action.motion.type = VMT_LINE;
               }else{
                    return VCS_INVALID;
               }
               break;
          case '}':
               built_action.motion.type = VMT_NEXT_BLANK_LINE;
               break;
          case '{':
               built_action.motion.type = VMT_PREV_BLANK_LINE;
               break;
          }
     }

     *action = built_action;
     return VCS_COMPLETE;
}

static int ispunct_or_iswordchar(int c)
{
     return ce_ispunct(c) || ce_iswordchar(c);
}

static int isnotquote(int c)
{
     return c != '"';
}

static int isnotsinglequote(int c)
{
     return c != '\'';
}

bool vim_action_get_range(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                          VimBufferState_t* vim_buffer_state, VimActionRange_t* action_range)
{
     action_range->start = *cursor;
     action_range->end = action_range->start;

     action_range->yank_mode = YANK_NORMAL;

     // setup the start and end if we are in visual mode
     if(action->motion.type == VMT_VISUAL_RANGE){
          Point_t calc_visual_start = *cursor;
          int64_t visual_length = action->motion.visual_length;
          if(!action->motion.visual_start_after) visual_length = -visual_length;
          ce_advance_cursor(buffer, &calc_visual_start, visual_length);
          action_range->start = *cursor;
          action_range->end = calc_visual_start;
     }else if(action->motion.type == VMT_VISUAL_LINE){
          Point_t calc_visual_start = *cursor;

          calc_visual_start.y += action->motion.visual_lines;

          // in visual line mode, we need to figure out which points are first/last so that we can set the
          // start/end 'x's accordingly, to the beginning and end of line
          const Point_t* a = cursor;
          const Point_t* b = &calc_visual_start;

          ce_sort_points(&a, &b);

          action_range->start = *a;
          action_range->end = *b;
          action_range->start.x = 0;
          int last_line = buffer->line_count - 1;
          int line = action_range->end.y;
          if(last_line < 0) return false;
          if(line > last_line) line = last_line;
          action_range->end.x = strlen(buffer->lines[line]);
          action_range->yank_mode = YANK_LINE;
     }else if(buffer->line_count){ // can't do motions without a buffer !
          int64_t multiplier = action->multiplier * action->motion.multiplier;

          for(int64_t i = 0; i < multiplier; ++i){
               // get range based on motion
               switch(action->motion.type){
               default:
               case VMT_NONE:
                    break;
               case VMT_LEFT:
                    action_range->end.x--;
                    if(action_range->end.x < 0) action_range->end.x = 0;
                    break;
               case VMT_RIGHT:
               {
                    int64_t line_len = strlen(buffer->lines[action_range->end.y]);
                    action_range->end.x++;
                    if(action_range->end.x > line_len) action_range->end.x = line_len;
               } break;
               case VMT_UP:
                    action_range->end.y--;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    break;
               case VMT_DOWN:
                    action_range->end.y++;
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    break;
               case VMT_WORD_LITTLE:
                    ce_move_cursor_to_next_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_BIG:
                    ce_move_cursor_to_next_word(buffer, &action_range->end, false);
                    break;
               case VMT_WORD_BEGINNING_LITTLE:
                    ce_move_cursor_to_beginning_of_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_BEGINNING_BIG:
                    ce_move_cursor_to_beginning_of_word(buffer, &action_range->end, false);
                    break;
               case VMT_WORD_END_LITTLE:
                    ce_move_cursor_to_end_of_word(buffer, &action_range->end, true);
                    break;
               case VMT_WORD_END_BIG:
                    ce_move_cursor_to_end_of_word(buffer, &action_range->end, false);
                    break;
               case VMT_LINE:
                    action_range->start.x = 0;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_UP:
                    action_range->start.x = 0;
                    action_range->start.y--;
                    if(action_range->start.y < 0) action_range->start.y = 0;
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_DOWN:
                    action_range->start.x = 0;
                    action_range->end.y++;
                    if(action_range->end.y >= buffer->line_count) action_range->end.y = buffer->line_count - 1;
                    if(action_range->end.y < 0) action_range->end.y = 0;
                    action_range->end.x = strlen(buffer->lines[action_range->end.y]);
                    action_range->yank_mode = YANK_LINE;
                    break;
               case VMT_LINE_SOFT:
               {
                    ce_move_cursor_to_soft_beginning_of_line(buffer, &action_range->start);
                    ce_move_cursor_to_end_of_line(buffer, &action_range->end);
               } break;
               case VMT_FIND_NEXT_MATCHING_CHAR:
                    if(action->motion.match_char){
                         if(ce_move_cursor_forward_to_char(buffer, &action_range->end, action->motion.match_char)){
                              vim_state->find_char_state.motion_type = action->motion.type;
                              vim_state->find_char_state.ch = action->motion.match_char;
                         }
                    }else{
                         ce_move_cursor_forward_to_char(buffer, &action_range->end, vim_state->find_char_state.ch);
                    }
                    break;
               case VMT_FIND_PREV_MATCHING_CHAR:
                    if(action->motion.match_char){
                         if(ce_move_cursor_backward_to_char(buffer, &action_range->end, action->motion.match_char)){
                              vim_state->find_char_state.motion_type = action->motion.type;
                              vim_state->find_char_state.ch = action->motion.match_char;
                         }
                    }else{
                         ce_move_cursor_backward_to_char(buffer, &action_range->end, vim_state->find_char_state.ch);
                    }
                    break;
               case VMT_TO_NEXT_MATCHING_CHAR:
                    action_range->end.x++;
                    if(action->motion.match_char){
                         if(ce_move_cursor_forward_to_char(buffer, &action_range->end, action->motion.match_char)){
                              if(action->motion.match_char){
                                   vim_state->find_char_state.motion_type = action->motion.type;
                                   vim_state->find_char_state.ch = action->motion.match_char;
                              }
                              action_range->end.x--;
                              if(action_range->end.x < 0) action_range->end.x = 0;
                         }else{
                              action_range->end.x--;
                         }
                    }else{
                         if(ce_move_cursor_forward_to_char(buffer, &action_range->end, vim_state->find_char_state.ch)){
                              action_range->end.x--;
                              if(action_range->end.x < 0) action_range->end.x = 0;
                         }else{
                              action_range->end.x--;
                         }
                    }
                    break;
               case VMT_TO_PREV_MATCHING_CHAR:
               {
                    action_range->end.x--;
                    if(action->motion.match_char){
                         if(ce_move_cursor_backward_to_char(buffer, &action_range->end, action->motion.match_char)){
                              if(action->motion.match_char){
                                   vim_state->find_char_state.motion_type = action->motion.type;
                                   vim_state->find_char_state.ch = action->motion.match_char;
                              }
                              action_range->end.x++;
                              int64_t line_len = strlen(buffer->lines[action_range->end.y]);
                              if(action_range->end.x > line_len) action_range->end.x = line_len;
                         }else{
                              action_range->end.x++;
                         }
                    }else{
                         if(ce_move_cursor_backward_to_char(buffer, &action_range->end, vim_state->find_char_state.ch)){
                              action_range->end.x++;
                              int64_t line_len = strlen(buffer->lines[action_range->end.y]);
                              if(action_range->end.x > line_len) action_range->end.x = line_len;
                         }else{
                              action_range->end.x++;
                         }
                    }
               }break;
               case VMT_BEGINNING_OF_FILE:
                    ce_move_cursor_to_beginning_of_file(buffer, &action_range->end);
                    break;
               case VMT_BEGINNING_OF_LINE_HARD:
                    ce_move_cursor_to_beginning_of_line(buffer, &action_range->end);
                    break;
               case VMT_BEGINNING_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_beginning_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_LINE_PASSED:
                    ce_move_cursor_to_end_of_line(buffer, &action_range->end);
                    action_range->end.x++;
                    break;
               case VMT_END_OF_LINE_HARD:
                    ce_move_cursor_to_end_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_LINE_SOFT:
                    ce_move_cursor_to_soft_end_of_line(buffer, &action_range->end);
                    break;
               case VMT_END_OF_FILE:
                    ce_move_cursor_to_end_of_file(buffer, &action_range->end);
                    break;
               case VMT_INSIDE_PAIR:
                    switch(action->motion.inside_pair){
                    case '"':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotquote)) return false;
                         if(action_range->start.x == 0) return false;
                         break;
                    case '\'':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotsinglequote)) return false;
                         if(action_range->start.x == 0) return false;
                         break;
                    case '(':
                    case ')':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ')')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '(')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '{':
                    case '}':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, '}')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '{')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '[':
                    case ']':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ']')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '[')) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    default:
                         return false;
                    }

                    if(action_range->start.x == action_range->end.x && action_range->start.y == action_range->end.y) return false;
                    break;
               case VMT_INSIDE_WORD_LITTLE:
                    ce_get_word_at_location(buffer, *cursor, &action_range->start, &action_range->end);
                    break;
               case VMT_INSIDE_WORD_BIG:
               {
                    char curr_char;
                    if(!ce_get_char(buffer, action_range->start, &curr_char)) return false;

                    if(isblank(curr_char)){
                         ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isblank);
                    }else{
                         assert(ispunct_or_iswordchar(curr_char));
                         ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, ispunct_or_iswordchar);
                    }
               } break;
               case VMT_AROUND_PAIR:
                    switch(action->motion.inside_pair){
                    case '"':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotquote)) return false;
                         ce_advance_cursor(buffer, &action_range->start, -1);
                         ce_advance_cursor(buffer, &action_range->end, 1);
                         break;
                    case '\'':
                         if(!ce_get_homogenous_adjacents(buffer, &action_range->start, &action_range->end, isnotsinglequote)) return false;
                         ce_advance_cursor(buffer, &action_range->start, 1);
                         ce_advance_cursor(buffer, &action_range->end, -1);
                         break;
                    case '(':
                    case ')':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ')')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '(')) return false;
                         break;
                    case '{':
                    case '}':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, '}')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '{')) return false;
                         break;
                    case '[':
                    case ']':
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->start, ']')) return false;
                         if(!ce_move_cursor_to_matching_pair(buffer, &action_range->end, '[')) return false;
                         break;
                    default:
                         return false;
                    }

                    if(action_range->start.x == action_range->end.x && action_range->start.y == action_range->end.y) return false;
                    break;
                    // TIME TO SLURP
#define SLURP_RIGHT(condition)                                                              \
                    do{ action_range->end.x++; if(!ce_get_char(buffer, action_range->end, &c)) break; }while(condition(c)); \
                    action_range->end.x--;

#define SLURP_LEFT(condition)                                                                   \
                    do{ action_range->start.x--; if(!ce_get_char(buffer, action_range->start, &c)) break; }while(condition(c)); \
                    action_range->start.x++;

               case VMT_AROUND_WORD_LITTLE:
               {
                    char c;
                    if(!ce_get_char(buffer, action_range->start, &c)) return false;

                    if(ce_iswordchar(c)){
                         SLURP_RIGHT(ce_iswordchar);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ce_iswordchar);

                         }else if(ce_ispunct(c)){
                              SLURP_LEFT(ce_iswordchar);
                              SLURP_LEFT(isblank);
                         }
                    }else if(ce_ispunct(c)){
                         SLURP_RIGHT(ce_ispunct);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ce_ispunct);

                         }else if(ce_ispunct(c)){
                              SLURP_LEFT(ce_ispunct);
                              SLURP_LEFT(isblank);
                         }
                    }else{
                         assert(isblank(c));
                         SLURP_RIGHT(isblank);

                         if(ce_ispunct(c)){
                              SLURP_RIGHT(ce_ispunct);
                              SLURP_LEFT(isblank);

                         }else if(ce_iswordchar(c)){
                              SLURP_RIGHT(ce_iswordchar);
                              SLURP_LEFT(isblank);

                         }else{
                              SLURP_LEFT(isblank);

                              if(ce_ispunct(c)){
                                   SLURP_LEFT(ce_ispunct);

                              }else if(ce_iswordchar(c)){
                                   SLURP_LEFT(ce_iswordchar);
                              }
                         }
                    }
               } break;
               case VMT_AROUND_WORD_BIG:
               {
                    char c;
                    if(!ce_get_char(buffer, action_range->start, &c)) return false;

                    if(ispunct_or_iswordchar(c)){
                         SLURP_RIGHT(ispunct_or_iswordchar);

                         if(isblank(c)){
                              SLURP_RIGHT(isblank);
                              SLURP_LEFT(ispunct_or_iswordchar);
                         }
                    }else{
                         assert(isblank(c));
                         SLURP_RIGHT(isblank);

                         if(ispunct_or_iswordchar(c)){
                              SLURP_RIGHT(ispunct_or_iswordchar);
                              SLURP_LEFT(isblank);

                         }else{
                              SLURP_LEFT(isblank);
                              SLURP_LEFT(ispunct_or_iswordchar);
                         }
                    }
               } break;
               case VMT_VISUAL_SWAP_WITH_CURSOR:
               {
                    Point_t tmp = *cursor;
                    *cursor = vim_state->visual_start;
                    vim_state->visual_start = tmp;
               } break;
               case VMT_MATCHING_PAIR:
               {
                    char matchee;
                    if(!ce_get_char(buffer, action_range->end, &matchee)) break;
                    ce_move_cursor_to_matching_pair(buffer, &action_range->end, matchee);
               } break;
               case VMT_NEXT_BLANK_LINE:
               {
                    int64_t last_line = buffer->line_count - 1;
                    action_range->end.y++;
                    while(action_range->end.y < last_line && strlen(buffer->lines[action_range->end.y])){
                         action_range->end.y++;
                    }
               } break;
               case VMT_PREV_BLANK_LINE:
                    action_range->end.y--;
                    while(action_range->end.y > 0 && strlen(buffer->lines[action_range->end.y])){
                         action_range->end.y--;
                    }
                    break;
               case VMT_SEARCH_WORD_UNDER_CURSOR:
               {
                    Point_t word_start, word_end;
                    if(!ce_get_word_at_location(buffer, *cursor, &word_start, &word_end)) break;
                    char* search_str = ce_dupe_string(buffer, word_start, word_end);
                    int64_t search_len = strlen(search_str);
                    int64_t word_search_len = (sizeof RE_WORD_BOUNDARY_START-1) + search_len + (sizeof RE_WORD_BOUNDARY_END-1);
                    char* word_search_str = malloc(word_search_len + 1);
                    snprintf(word_search_str, word_search_len + 1, RE_WORD_BOUNDARY_START "%s" RE_WORD_BOUNDARY_END, search_str);
                    word_search_str[word_search_len] = 0;
                    free(search_str);

                    vim_yank_add(&vim_state->yank_head, '/', word_search_str, YANK_NORMAL);

                    int rc = regcomp(&vim_state->search.regex, word_search_str, REG_EXTENDED);
                    vim_state->search.valid_regex = (rc == 0);
               }
               // NOTE: fall through intentionally
               case VMT_SEARCH:
               {
                    if(!vim_state->search.valid_regex){
                         return false;
                    }

                    vim_state->search.direction = action->motion.search_direction;
                    VimYankNode_t* yank = vim_yank_find(vim_state->yank_head, '/');
                    if(yank){
                         assert(yank->mode == YANK_NORMAL);

                         if(vim_state->search.direction == CE_UP){
                              ce_move_cursor_to_beginning_of_word(buffer, &action_range->end, true);
                         }

                         Point_t search_start = action_range->end;
                         if(vim_state->search.direction == CE_DOWN) search_start.x++;

                         if(search_start.y >= buffer->line_count) return false;

                         if(search_start.x >= ce_last_index(buffer->lines[search_start.y])){
                              search_start.x = 0;
                              search_start.y++;
                         }

                         if(search_start.y >= buffer->line_count) return false;

                         Point_t match;
                         int64_t match_len;
                         if(ce_find_regex(buffer, search_start, &vim_state->search.regex, &match, &match_len, vim_state->search.direction)){
                              ce_set_cursor(buffer, &action_range->end, match);
                         }else{
                              ce_message("failed to find match for '%s'", yank->text);
                              action_range->end = *cursor;
                              return false;
                         }
                    }
               } break;
               case VMT_GOTO_MARK:
               {
                    Point_t* marked_location = vim_mark_find(vim_buffer_state->mark_head, action->motion.reg);
                    if(marked_location) {
                         ce_move_cursor_to_soft_beginning_of_line(buffer, marked_location);
                         action_range->end = *marked_location;
                    }
               } break;
               }
          }

          // some rules when we are deleting or pasting outside of visual mode
          if(action->change.type == VCT_DELETE || action->change.type == VCT_YANK || action->change.type == VCT_FLIP_CASE){
               // deleting before the cursor never includes the character the cursor is on
               if(ce_point_after(action_range->start, action_range->end)){
                    if(action_range->start.x > 0) action_range->start.x--;
               // deleting to the next word does not include the next word's first character
               }else if(action->motion.type == VMT_WORD_LITTLE || action->motion.type == VMT_WORD_BIG){
                    if(action_range->end.y != action_range->start.y){
                         action_range->end.y = action_range->start.y;
                         action_range->end.x = ce_last_index(buffer->lines[action_range->end.y]);
                    }else if(action_range->end.x > 0){
                         action_range->end.x--;
                    }
               // going left means just the character left of the cursor
               }else if(action->motion.type == VMT_LEFT){
                    if(action_range->end.x + 1 <= ce_last_index(buffer->lines[action_range->start.y])){
                         action_range->end.x++;
                    }
               // going right means the character under the cursor
               }else if(action->motion.type == VMT_RIGHT){
                    if(action_range->end.x > 0) action_range->end.x--;
               }
          }
     }

     action_range->sorted_start = &action_range->start;
     action_range->sorted_end = &action_range->end;

     ce_sort_points(&action_range->sorted_start, &action_range->sorted_end);

     return true;
}

const char* get_comment_string(BufferFileType_t type)
{
     switch(type){
     default:
          break;
     case BFT_C:
          return VIM_C_COMMENT_STRING;
     case BFT_PYTHON:
          return VIM_PYTHON_COMMENT_STRING;
     case BFT_CONFIG:
          return VIM_CONFIG_COMMENT_STRING;
     }

     return NULL;
}

bool vim_action_apply(VimAction_t* action, Buffer_t* buffer, Point_t* cursor, VimState_t* vim_state,
                      BufferCommitNode_t** commit_tail, VimBufferState_t* vim_buffer_state)
{
     VimActionRange_t action_range;
     BufferCommitChain_t chain = vim_state->playing_macro ? BCC_KEEP_GOING : BCC_STOP;

     if(!vim_action_get_range(action, buffer, cursor, vim_state, vim_buffer_state, &action_range) ) return false;

     // perform action on range
     switch(action->change.type){
     default:
          break;
     case VCT_MOTION:
          if(action->end_in_vim_mode == VM_INSERT){
               vim_state->insert_start = *cursor;
          }

          *cursor = action_range.end;

          if(action->motion.type == VMT_UP ||
             action->motion.type == VMT_DOWN){
               cursor->x = vim_buffer_state->cursor_save_column;
          }

          if(vim_state->mode == VM_VISUAL_RANGE){
               // expand the selection for some motions
               if(ce_point_after(vim_state->visual_start, *cursor) &&
                  ce_point_after(*action_range.sorted_end, vim_state->visual_start)){
                     vim_state->visual_start = *action_range.sorted_end;
               }else if(ce_point_after(*cursor, vim_state->visual_start) &&
                        ce_point_after(vim_state->visual_start, *action_range.sorted_start)){
                     vim_state->visual_start = *action_range.sorted_start;
               }
          }
          break;
     case VCT_DELETE:
     {
          *cursor = *action_range.sorted_start;

          char* commit_string = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);
          if(!commit_string) return false;

          int64_t len = ce_compute_length(buffer, *action_range.sorted_start, *action_range.sorted_end);

          if(!ce_remove_string(buffer, *action_range.sorted_start, len)){
               free(commit_string);
               return false;
          }

          if(action->yank){
               char* yank_string = strdup(commit_string);
               if(action_range.yank_mode == YANK_LINE && yank_string[len-1] == NEWLINE) yank_string[len-1] = 0;
               vim_yank_add(&vim_state->yank_head, action->change.reg ? action->change.reg : '"', yank_string, action_range.yank_mode);
          }

          ce_commit_remove_string(commit_tail, *action_range.sorted_start, *cursor, *action_range.sorted_start, commit_string, chain);
     } break;
     case VCT_PASTE_BEFORE:
     {
          VimYankNode_t* yank = vim_yank_find(vim_state->yank_head, action->change.reg ? action->change.reg : '"');

          if(!yank) return false;

          switch(yank->mode){
          default:
               return false;
          case YANK_NORMAL:
          {
               if(!ce_insert_string(buffer, *action_range.sorted_start, yank->text)){
                    return false;
               }

               ce_commit_insert_string(commit_tail,
                                       *action_range.sorted_start, *action_range.sorted_start, *action_range.sorted_start,
                                       strdup(yank->text), chain);
          } break;
          case YANK_LINE:
          {
               size_t len = strlen(yank->text);
               char* save_str = malloc(len + 2); // newline and '\0'
               Point_t insert_loc = {0, cursor->y};
               Point_t cursor_loc = {0, cursor->y};

               save_str[len] = '\n'; // append a new line to create a line
               save_str[len+1] = '\0';
               memcpy(save_str, yank->text, len);

               if(!ce_insert_string(buffer, insert_loc, save_str)){
                    return false;
               }

               ce_commit_insert_string(commit_tail,
                                       insert_loc, *cursor, cursor_loc,
                                       save_str, chain);
          } break;
          }
     } break;
     case VCT_PASTE_AFTER:
     {
          VimYankNode_t* yank = vim_yank_find(vim_state->yank_head, action->change.reg ? action->change.reg : '"');

          if(!yank) return false;

          switch(yank->mode){
          default:
               return false;
          case YANK_NORMAL:
          {
               Point_t insert_cursor = *cursor;
               int64_t yank_len = strlen(yank->text);

               if(buffer->lines[cursor->y][0]){
                    insert_cursor.x++; // don't increment x for blank lines
               }else{
                    assert(cursor->x == 0);
                    // if the line is empty we cannot paste after the first character, so when we move the
                    // cursor at the end, we need to account for 1 less character
                    yank_len--;
               }

               if(!ce_insert_string(buffer, insert_cursor, yank->text)){
                    return false;
               }

               ce_commit_insert_string(commit_tail,
                                       insert_cursor, *action_range.sorted_start, *action_range.sorted_start,
                                       strdup(yank->text), chain);
               ce_advance_cursor(buffer, cursor, yank_len);
          } break;
          case YANK_LINE:
          {
               size_t len = strlen(yank->text);
               char* save_str = malloc(len + 2); // newline and '\0'
               Point_t cursor_loc = {0, cursor->y + 1};
               Point_t insert_loc = {strlen(buffer->lines[cursor->y]), cursor->y};

               save_str[0] = '\n'; // prepend a new line to create a line
               memcpy(save_str + 1, yank->text, len + 1); // also copy the '\0'

               if(!ce_insert_string(buffer, insert_loc, save_str)){
                    return false;
               }

               ce_commit_insert_string(commit_tail,
                                       insert_loc, *cursor, cursor_loc,
                                       save_str, chain);
               *cursor = cursor_loc;
          } break;
          }
     } break;
     case VCT_CHANGE_CHAR:
     {
          char prev_char;

          if(!ce_get_char(buffer, *action_range.sorted_start, &prev_char)) return false;
          if(!ce_set_char(buffer, *action_range.sorted_start, action->change.change_char)) return false;

          ce_commit_change_char(commit_tail, *action_range.sorted_start, *cursor, *action_range.sorted_start,
                                action->change.change_char, prev_char, chain);
     } break;
     case VCT_YANK:
     {
          char* save_zero = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);
          char* save_quote = ce_dupe_string(buffer, *action_range.sorted_start, *action_range.sorted_end);

          if(action_range.yank_mode == YANK_LINE){
               int64_t last_index = strlen(save_zero) - 1;
               if(last_index >= 0 && save_zero[last_index] == NEWLINE){
                    save_zero[last_index] = 0;
                    save_quote[last_index] = 0;
               }
          }

          vim_yank_add(&vim_state->yank_head, '0', save_zero, action_range.yank_mode);
          vim_yank_add(&vim_state->yank_head, action->change.reg ? action->change.reg : '"', save_quote,
                       action_range.yank_mode);
     } break;
     case VCT_INDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
                    if(strlen(buffer->lines[i]) == 0) continue;
                    Point_t loc = {0, i};
                    if(!ce_insert_string(buffer, loc, TAB_STRING)) return false;
                    ce_commit_insert_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
               }

               if(*commit_tail) (*commit_tail)->commit.chain = chain;
          }
     } break;
     case VCT_UNINDENT:
     {
          if(action->motion.type == VMT_LINE || action->motion.type == VMT_LINE_UP ||
             action->motion.type == VMT_LINE_DOWN || action->motion.type == VMT_VISUAL_RANGE ||
             action->motion.type == VMT_VISUAL_LINE){
               for(int l = action_range.sorted_start->y; l <= action_range.sorted_end->y; ++l){
                    int64_t whitespace_count = 0;
                    const int64_t tab_len = strlen(TAB_STRING);
                    for(int i = 0; i < tab_len; ++i){
                         if(isblank(buffer->lines[l][i])){
                              whitespace_count++;
                         }else{
                              break;
                         }
                    }

                    if(whitespace_count){
                         Point_t loc = {0, l};
                         if(!ce_remove_string(buffer, loc, whitespace_count)) return false;
                         ce_commit_remove_string(commit_tail, loc, *cursor, *cursor, strdup(TAB_STRING), BCC_KEEP_GOING);
                    }
               }

               if(*commit_tail) (*commit_tail)->commit.chain = chain;
          }
     } break;
     case VCT_COMMENT:
     {
          const char* comment = get_comment_string(buffer->type);
          if(!comment) break;

          for(int64_t i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
               if(!strlen(buffer->lines[i])) continue;

               Point_t soft_beginning = {0, i};
               ce_move_cursor_to_soft_beginning_of_line(buffer, &soft_beginning);

               if(!ce_insert_string(buffer, soft_beginning, comment)){
                    return false;
               }

               ce_commit_insert_string(commit_tail, soft_beginning, *cursor, *cursor,
                                       strdup(comment), BCC_KEEP_GOING);
          }

          if(*commit_tail) (*commit_tail)->commit.chain = chain;
     } break;
     case VCT_UNCOMMENT:
     {
          const char* comment = get_comment_string(buffer->type);
          if(!comment) break;

          for(int64_t i = action_range.sorted_start->y; i <= action_range.sorted_end->y; ++i){
               Point_t soft_beginning = {0, i};
               ce_move_cursor_to_soft_beginning_of_line(buffer, &soft_beginning);

               if(strncmp(buffer->lines[i] + soft_beginning.x, comment,
                          strlen(comment)) != 0) continue;

               if(!ce_remove_string(buffer, soft_beginning, strlen(comment))){
                    return false;
               }

               ce_commit_remove_string(commit_tail, soft_beginning, *cursor, *cursor,
                                       strdup(comment), BCC_KEEP_GOING);
          }

          if(*commit_tail) (*commit_tail)->commit.chain = chain;
     } break;
     case VCT_FLIP_CASE:
     {
          Point_t itr = *action_range.sorted_start;

          do{
               char prev_char = 0;
               if(!ce_get_char(buffer, itr, &prev_char)) return false;

               if(isalpha(prev_char)) {
                    char new_char = 0;

                    if(isupper(prev_char)){
                         new_char = tolower(prev_char);
                    }else{
                         new_char = toupper(prev_char);
                    }

                    if(!ce_set_char(buffer, itr, new_char)) return false;
                    ce_commit_change_char(commit_tail, itr, itr, itr, new_char, prev_char, BCC_KEEP_GOING);
               }

               ce_advance_cursor(buffer, &itr, 1);
          } while(!ce_point_after(itr, *action_range.sorted_end));

          if(*commit_tail) (*commit_tail)->commit.chain = chain;
     } break;
     case VCT_JOIN_LINE:
     {
          if(action_range.sorted_start->y >= buffer->line_count - 1) break; // nothing to join

          Point_t next_line_start = {0, action_range.sorted_start->y + 1};
          Point_t next_line_join = next_line_start;
          ce_move_cursor_to_soft_beginning_of_line(buffer, &next_line_join);

          int64_t whitespace_to_delete = next_line_join.x - next_line_start.x;
          if(whitespace_to_delete){
               next_line_join.x--;
               char* save_whitespace = ce_dupe_string(buffer, next_line_start, next_line_join);
               if(!ce_remove_string(buffer, next_line_start, whitespace_to_delete)){
                    return false;
               }

               ce_commit_remove_string(commit_tail, next_line_start, *action_range.sorted_start, next_line_start,
                                       save_whitespace, BCC_KEEP_GOING);
          }

          char* save_str = strdup(buffer->lines[next_line_start.y]);
          char* save_line = ce_dupe_line(buffer, next_line_start.y);
          Point_t join_loc = {strlen(buffer->lines[action_range.sorted_start->y]), action_range.sorted_start->y};

          if(ce_join_line(buffer, action_range.sorted_start->y)){
               ce_commit_insert_string(commit_tail, join_loc, *action_range.sorted_start, join_loc,
                                       save_str, BCC_KEEP_GOING);
               ce_commit_remove_string(commit_tail, next_line_start, *action_range.sorted_start, next_line_start,
                                       save_line, BCC_KEEP_GOING);
               *cursor = join_loc;
               if(ce_insert_string(buffer, *cursor, " ")){
                    ce_commit_insert_string(commit_tail, join_loc, *action_range.sorted_start, join_loc,
                                            strdup(" "), chain);
               }
          }else{
               free(save_str);
               free(save_line);
               return false;
          }
     } break;
     case VCT_OPEN_ABOVE:
     {
          Point_t begin_line = {0, cursor->y};

          // indent if necessary
          int64_t indent_len = ce_get_indentation_for_line(buffer, begin_line, strlen(TAB_STRING));
          char* indent_nl = malloc(sizeof '\n' + indent_len + sizeof '\0');
          memset(&indent_nl[0], ' ', indent_len);
          indent_nl[indent_len] = '\n';
          indent_nl[indent_len + 1] = '\0';

          if(!ce_insert_string(buffer, begin_line, indent_nl)){
               return false;
          }

          *cursor = (Point_t){indent_len, cursor->y};
          ce_commit_insert_string(commit_tail, begin_line, *cursor, *cursor, indent_nl, BCC_KEEP_GOING);
     } break;
     case VCT_OPEN_BELOW:
     {
          Point_t end_of_line = *cursor;
          end_of_line.x = 0;
          if(cursor->y < buffer->line_count) end_of_line.x = strlen(buffer->lines[cursor->y]);

          // indent if necessary
          int64_t indent_len = ce_get_indentation_for_line(buffer, end_of_line, strlen(TAB_STRING));
          char* nl_indent = malloc(sizeof '\n' + indent_len + sizeof '\0');
          nl_indent[0] = '\n';
          memset(&nl_indent[1], ' ', indent_len);
          nl_indent[1 + indent_len] = '\0';

          if(!ce_insert_string(buffer, end_of_line, nl_indent)){
               return false;
          }

          Point_t save_cursor = *cursor;
          *cursor = (Point_t){indent_len, cursor->y + 1};
          ce_commit_insert_string(commit_tail, end_of_line, save_cursor, *cursor, nl_indent, BCC_KEEP_GOING);
     } break;
     case VCT_SET_MARK:
     {
          vim_mark_add(&vim_buffer_state->mark_head, action->change.reg, cursor);
     } break;
     case VCT_RECORD_MACRO:
          if(vim_state->recording_macro){
               int* built_macro = ce_keys_get_string(vim_state->record_macro_head);

               if(built_macro[0]){
                    vim_macro_add(&vim_state->macro_head, vim_state->recording_macro, built_macro);
               }else{
                    free(built_macro);
               }

               // override commit history to make our macro undo-able with 1 undo
               BufferCommitNode_t* itr = *commit_tail;

               // see if the commit we started recording at is still in the history
               while(itr){
                    if(itr == vim_state->record_start_commit_tail) break;
                    itr = itr->prev;
               }

               while(itr && itr->next){
                    itr->commit.chain = BCC_KEEP_GOING;
                    itr = itr->next;
               }

               if(itr) itr->commit.chain = BCC_STOP;

               vim_stop_recording_macro(vim_state);

               vim_macro_commits_free(&vim_state->macro_commit_current);
          }else{
               vim_state->recording_macro = action->change.reg;
               ce_keys_free(&vim_state->record_macro_head);
               vim_state->record_start_commit_tail = *commit_tail;
               vim_macro_commits_init(&vim_state->macro_commit_current);
          }
          break;
     case VCT_PLAY_MACRO:
     {
          if(vim_state->playing_macro == action->change.reg){
               ce_message("attempted to play macro in register '%c' inside itself", action->change.reg);
               break;
          }

          VimMacroNode_t* macro = vim_macro_find(vim_state->macro_head, action->change.reg);
          if(!macro){
               ce_message("no macro defined in register '%c'", action->change.reg);
               break;
          }

          KeyNode_t* save_command_head = vim_state->command_head;
          vim_state->command_head = NULL;
          vim_state->playing_macro = action->change.reg;

          for(int64_t i = 0; i < action->multiplier; ++i){
               bool unhandled_key = false;
               int* macro_itr = macro->command;
               while(*macro_itr){
                    VimKeyHandlerResult_t vkh_result =  vim_key_handler(*macro_itr, vim_state, buffer, cursor, commit_tail,
                                                                        vim_buffer_state, false);

                    if(vkh_result.type == VKH_UNHANDLED_KEY){
                         unhandled_key = true;
                         break;
                    }else if(vkh_result.type == VKH_COMPLETED_ACTION_FAILURE){
                         break;
                    }

                    macro_itr++;
               }

               ce_keys_free(&vim_state->command_head);

               if(*commit_tail) (*commit_tail)->commit.chain = BCC_STOP;

               if(unhandled_key) break;
          }

          vim_state->playing_macro = 0;
          vim_state->command_head = save_command_head;
     } break;
     }

     vim_state->mode = action->end_in_vim_mode;

     if(action->end_in_vim_mode == VM_INSERT){
          // the insert mode clamp
          int64_t line_len = 0;
          if(cursor->y < buffer->line_count) line_len = strlen(buffer->lines[cursor->y]);
          if(cursor->x > line_len) cursor->x = line_len;

          // if we end in insert mode, make sure undo is chained with the action
          if(action->change.type != VCT_NONE && action->change.type != VCT_MOTION && *commit_tail) (*commit_tail)->commit.chain = BCC_KEEP_GOING;
     }else{
          Point_t old_cursor = *cursor;
          ce_clamp_cursor(buffer, cursor);

          if(old_cursor.x == cursor->x &&
             old_cursor.y == cursor->y){
               vim_buffer_state->cursor_save_column = cursor->x;
          }
     }

     return true;
}

void vim_enter_normal_mode(VimState_t* vim_state)
{
     vim_state->mode = VM_NORMAL;
}

bool vim_enter_insert_mode(VimState_t* vim_state, Buffer_t* buffer)
{
     if(buffer->status == BS_READONLY) return false;

     vim_state->mode = VM_INSERT;
     return true;
}

void vim_enter_visual_range_mode(VimState_t* vim_state, Point_t cursor)
{
     vim_state->mode = VM_VISUAL_RANGE;
     vim_state->visual_start = cursor;
}

void vim_enter_visual_line_mode(VimState_t* vim_state, Point_t cursor)
{
     vim_state->mode = VM_VISUAL_LINE;
     vim_state->visual_start = cursor;
}

void vim_stop_recording_macro(VimState_t* vim_state)
{
     vim_state->recording_macro = 0;
     ce_keys_free(&vim_state->record_macro_head);
     vim_state->last_macro_command_begin = NULL;
}

char* vim_command_string_to_char_string(const int* int_str)
{
     // build length
     size_t len = 1; // account for NULL terminator
     const int* int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               len++;
          }else{
               switch(*int_itr){
               default:
                    len++; // going to fill in with '~' for now
                    break;
               case KEY_BACKSPACE:
               case KEY_ESCAPE:
               case KEY_ENTER:
               case KEY_TAB:
               case KEY_UP:
               case KEY_DOWN:
               case KEY_LEFT:
               case KEY_RIGHT:
               case '\\':
                    len += 2;
                    break;
               }
          }

          int_itr++;
     }

     char* char_str = malloc(len);
     if(!char_str) return NULL;

     char* char_itr = char_str;
     int_itr = int_str;
     while(*int_itr){
          if(isprint(*int_itr)){
               *char_itr = *int_itr;
               char_itr++;
          }else{
               switch(*int_itr){
               default:
                    *char_itr = '~';
                    char_itr++;
                    break;
               case KEY_BACKSPACE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'b'; char_itr++;
                    break;
               case KEY_ESCAPE:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'e'; char_itr++;
                    break;
               case KEY_ENTER:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'r'; char_itr++;
                    break;
               case KEY_TAB:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 't'; char_itr++;
                    break;
               case KEY_UP:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'u'; char_itr++;
                    break;
               case KEY_DOWN:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'd'; char_itr++;
                    break;
               case KEY_LEFT:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'l'; char_itr++;
                    break;
               case KEY_RIGHT:
                    *char_itr = '\\'; char_itr++;
                    *char_itr = 'i'; char_itr++; // NOTE: not happy with 'i'
                    break;
               case '\\':
                    *char_itr = '\\'; char_itr++;
                    *char_itr = '\\'; char_itr++;
                    break;
               }
          }

          int_itr++;
     }

     char_str[len - 1] = 0;

     return char_str;
}

int* vim_char_string_to_command_string(const char* char_str)
{
     // we can just use the strlen, and it'll be over allocated because the command string will always be
     // the same size or small than the char string
     size_t str_len = strlen(char_str);

     int* int_str = malloc((str_len + 1) * sizeof(*int_str));
     if(!int_str) return NULL;

     int* int_itr = int_str;
     const char* char_itr = char_str;
     while(*char_itr){
          if(!isprint(*char_itr)){
               free(int_str);
               return NULL;
          }

          if(*char_itr == '\\'){
               char_itr++;
               switch(*char_itr){
               default:
                    free(int_str);
                    return NULL;
               case 'b':
                    *int_itr = KEY_BACKSPACE;
                    break;
               case 'e':
                    *int_itr = KEY_ESCAPE;
                    break;
               case 'r':
                    *int_itr = KEY_ENTER;
                    break;
               case 't':
                    *int_itr = KEY_TAB;
                    break;
               case 'u':
                    *int_itr = KEY_UP;
                    break;
               case 'd':
                    *int_itr = KEY_DOWN;
                    break;
               case 'l':
                    *int_itr = KEY_LEFT;
                    break;
               case 'i':
                    *int_itr = KEY_RIGHT;
                    break;
               case '\\':
                    *int_itr = '\\';
                    break;
               }

               char_itr++;
               int_itr++;
          }else{
               *int_itr = *char_itr;
               char_itr++;
               int_itr++;
          }
     }

     *int_itr = 0; // NULL terminate

     return int_str;
}
