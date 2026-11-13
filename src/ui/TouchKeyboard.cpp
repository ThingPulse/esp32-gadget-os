#include "ui/TouchKeyboard.h"
#include <cstring>
#include <array>
namespace {
const char *lower[]={"q","w","e","r","t","y","u","i","o","p","\n",
 "a","s","d","f","g","h","j","k","l","\n",
 "z","x","c","v","b","n","m",".","_","-","\n",
 "ABC","1#",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_BACKSPACE,LV_SYMBOL_KEYBOARD,LV_SYMBOL_OK,""};
const char *upper[]={"Q","W","E","R","T","Y","U","I","O","P","\n",
 "A","S","D","F","G","H","J","K","L","\n",
 "Z","X","C","V","B","N","M",".","_","-","\n",
 "abc","1#",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_BACKSPACE,LV_SYMBOL_KEYBOARD,LV_SYMBOL_OK,""};
lv_buttonmatrix_ctrl_t controls[37];
// Keep the single-line layout intact; insert Enter immediately before Done.
template<size_t N>
std::array<const char *, N+1> with_enter(const char *(&map)[N]) {
 std::array<const char *, N+1> result{};
 for(size_t i=0;i<N-2;++i)result[i]=map[i];
 result[N-2]=LV_SYMBOL_NEW_LINE;
 result[N-1]=map[N-2];result[N]=map[N-1];
 return result;
}
const auto multiline_lower=with_enter(lower);
const auto multiline_upper=with_enter(upper);
const char *symbols[]={"1","2","3","4","5","6","7","8","9","0",LV_SYMBOL_BACKSPACE,"\n",
 "abc","+","&","/","*","=","%","!","?","#","<",">","\n",
 "\\","@","$","(",")","{","}","[","]",";","\"","'","\n",
 LV_SYMBOL_KEYBOARD,LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
const auto multiline_symbols=with_enter(symbols);
lv_buttonmatrix_ctrl_t multiline_controls[38];
lv_buttonmatrix_ctrl_t symbol_controls[41];
lv_buttonmatrix_ctrl_t single_symbol_controls[40];
void configure_controls(lv_obj_t *keyboard){
 lv_keyboard_set_popovers(keyboard,true);
 auto map=lv_buttonmatrix_get_map(keyboard);uint32_t id=0;
 for(size_t i=0;map[i]&&map[i][0];++i){
  if(!std::strcmp(map[i],"\n"))continue;
  lv_buttonmatrix_set_button_ctrl(keyboard,id,LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
  if(!std::strcmp(map[i],LV_SYMBOL_BACKSPACE))lv_buttonmatrix_clear_button_ctrl(keyboard,id,LV_BUTTONMATRIX_CTRL_NO_REPEAT);
  else lv_buttonmatrix_set_button_ctrl(keyboard,id,LV_BUTTONMATRIX_CTRL_NO_REPEAT);
  ++id;
 }
}
}
void configure_touch_keyboard(lv_obj_t *keyboard, bool multiline){
 for(auto &control:controls)control=lv_buttonmatrix_ctrl_t(1|LV_BUTTONMATRIX_CTRL_POPOVER);
 for(int i=29;i<37;++i)controls[i]=lv_buttonmatrix_ctrl_t(1|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
 controls[32]=lv_buttonmatrix_ctrl_t(3|LV_BUTTONMATRIX_CTRL_CLICK_TRIG|LV_BUTTONMATRIX_CTRL_NO_REPEAT);
 for(auto &control:symbol_controls)control=lv_buttonmatrix_ctrl_t(1|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
 for(int i:{10,11,35,36,38,39,40})symbol_controls[i]=lv_buttonmatrix_ctrl_t(2|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
 symbol_controls[37]=lv_buttonmatrix_ctrl_t(6|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
 if(multiline){
  for(size_t i=0;i<37;++i)multiline_controls[i]=controls[i];
  multiline_controls[37]=lv_buttonmatrix_ctrl_t(1|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER,multiline_lower.data(),multiline_controls);
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_UPPER,multiline_upper.data(),multiline_controls);
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_SPECIAL,multiline_symbols.data(),symbol_controls);
 }else{
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER,lower,controls);
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_UPPER,upper,controls);
  for(size_t i=0;i<40;++i)single_symbol_controls[i]=symbol_controls[i];
  lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_SPECIAL,symbols,single_symbol_controls);
 }
 lv_obj_set_style_pad_all(keyboard,3,0);
 lv_obj_set_style_pad_column(keyboard,3,0);
 lv_obj_set_style_pad_row(keyboard,6,0);
 configure_controls(keyboard);
 lv_obj_add_event_cb(keyboard,[](lv_event_t *e){configure_controls(static_cast<lv_obj_t*>(lv_event_get_target(e)));},LV_EVENT_VALUE_CHANGED,nullptr);
}
