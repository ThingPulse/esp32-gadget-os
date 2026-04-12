#if __has_include(<lgfx/v1/platforms/sdl/Panel_sdl.hpp>)
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#endif

#if defined(SDL_h_)

void setup(void);
void loop(void);

__attribute__((weak))
int user_func(bool* running)
{
  setup();
  do
  {
    loop();
  } while (*running);
  return 0;
}

int main(int, char**)
{
  return lgfx::Panel_sdl::main(user_func);
}

#else
void setup(void);
void loop(void);

int main(int, char**)
{
  setup();
  while (true) loop();
  return 0;
}
#endif
