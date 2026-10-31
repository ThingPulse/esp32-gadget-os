#include "ui/TouchKeyboard.h"
#include <cstring>
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
void configure_touch_keyboard(lv_obj_t *keyboard){
 for(auto &control:controls)control=lv_buttonmatrix_ctrl_t(1|LV_BUTTONMATRIX_CTRL_POPOVER);
 for(int i=29;i<37;++i)controls[i]=lv_buttonmatrix_ctrl_t(1|LV_KEYBOARD_CTRL_BUTTON_FLAGS);
 controls[32]=lv_buttonmatrix_ctrl_t(3|LV_BUTTONMATRIX_CTRL_CLICK_TRIG|LV_BUTTONMATRIX_CTRL_NO_REPEAT);
 lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_LOWER,lower,controls);
 lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_TEXT_UPPER,upper,controls);
 lv_obj_set_style_pad_all(keyboard,3,0);
 lv_obj_set_style_pad_column(keyboard,3,0);
 lv_obj_set_style_pad_row(keyboard,6,0);
 configure_controls(keyboard);
 lv_obj_add_event_cb(keyboard,[](lv_event_t *e){configure_controls(static_cast<lv_obj_t*>(lv_event_get_target(e)));},LV_EVENT_VALUE_CHANGED,nullptr);
}
