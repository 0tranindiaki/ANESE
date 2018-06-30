#include "gui.h"

#include <cstdio>
#include <iostream>

#include <cfgpath.h>
#include <clara.hpp>
#include <SDL2_inprint.h>
#include <SimpleIni.h>

#include "common/util.h"
#include "common/serializable.h"

#include "nes/cartridge/cartridge.h"
#include "nes/joy/controllers/standard.h"
#include "nes/nes.h"

#include "fs/util.h"

int SDL_GUI::init(int argc, char* argv[]) {
  // --------------------------- Argument Parsing --------------------------- //

  bool show_help = false;
  auto cli
    = clara::Help(show_help)
    | clara::Opt(this->args.log_cpu)
        ["--log-cpu"]
        ("Output CPU execution over STDOUT")
    | clara::Opt(this->args.no_sav)
        ["--no-sav"]
        ("Don't load/create sav files")
    | clara::Opt(this->args.ppu_timing_hack)
        ["--alt-nmi-timing"]
        ("Enable NMI timing fix \n"
         "(fixes some games, eg: Bad Dudes, Solomon's Key)")
    | clara::Opt(this->args.record_fm2_path, "path")
        ["--record-fm2"]
        ("Record a movie in the fm2 format")
    | clara::Opt(this->args.replay_fm2_path, "path")
        ["--replay-fm2"]
        ("Replay a movie in the fm2 format")
    | clara::Opt(this->args.config_file, "path")
        ["--config"]
        ("Use custom config file")
    | clara::Arg(this->args.rom, "rom")
        ("an iNES rom");

  auto result = cli.parse(clara::Args(argc, argv));
  if(!result) {
    std::cerr << "Error: " << result.errorMessage() << "\n";
    std::cerr << cli;
    exit(1);
  }

  if (show_help) {
    std::cout << cli;
    exit(1);
  }

  // ------------------------- Config File Parsing ------------------------- -//

  // Get cross-platform config path (if no custom path specified)
  if (this->args.config_file == "") {
    char config_f_path [256];
    cfgpath::get_user_config_file(config_f_path, 256, "anese");
    this->args.config_file = config_f_path;
  }

  this->config.load(this->args.config_file.c_str());

  // --------------------------- Init SDL2 Common --------------------------- //

  fprintf(stderr, "[SDL2] Initializing SDL2 GUI\n");

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);

  this->sdl_common.window = SDL_CreateWindow(
    "anese",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    this->sdl_common.RES_X * this->config.window_scale,
    this->sdl_common.RES_Y * this->config.window_scale,
    SDL_WINDOW_RESIZABLE
  );

  this->sdl_common.renderer = SDL_CreateRenderer(
    this->sdl_common.window,
    -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );

  // Letterbox the screen in the window
  SDL_RenderSetLogicalSize(this->sdl_common.renderer,
    this->sdl_common.RES_X * this->sdl_common.SCREEN_SCALE,
    this->sdl_common.RES_Y * this->sdl_common.SCREEN_SCALE);
  // Allow opacity when drawing menu
  SDL_SetRenderDrawBlendMode(this->sdl_common.renderer, SDL_BLENDMODE_BLEND);

  /* Open the first available controller. */
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      this->sdl_common.controller = SDL_GameControllerOpen(i);
    }
  }

  // SDL_AudioSpec as, have;
  // as.freq = SDL_GUI::SAMPLE_RATE;
  // as.format = AUDIO_F32SYS;
  // as.channels = 1;
  // as.samples = 4096;
  // as.callback = nullptr; // use SDL_QueueAudio
  // this->sdl_common.nes_audiodev = SDL_OpenAudioDevice(NULL, 0, &as, &have, 0);
  // SDL_PauseAudioDevice(this->sdl_common.nes_audiodev, 0);

  // Setup SDL2_inprint font
  SDL2_inprint::inrenderer(this->sdl_common.renderer);
  SDL2_inprint::prepare_inline_font();

  /*----------  Init GUI modules  ----------*/
  this->emu  = new EmuModule(this->sdl_common, this->args, this->config);
  this->menu = new MenuModule(this->sdl_common, this->args, this->config, *this->emu);

  // TODO: put this somewhere else...
  strcpy(this->menu->menu.directory, this->config.roms_dir);

  // ------------------------------ NES Params ------------------------------ //

  if (this->args.log_cpu)         { this->emu->params.log_cpu         = true; }
  if (this->args.ppu_timing_hack) { this->emu->params.ppu_timing_hack = true; }
  this->emu->nes.updated_params();

  // ---------------------------- Movie Support ----------------------------- //

  if (this->args.replay_fm2_path != "") {
    bool did_load = this->emu->fm2_replay.init(this->args.replay_fm2_path.c_str());
    if (!did_load)
      fprintf(stderr, "[Replay][fm2] Movie loading failed!\n");
    fprintf(stderr, "[Replay][fm2] Movie successfully loaded!\n");
  }

  if (this->args.record_fm2_path != "") {
    bool did_load = this->emu->fm2_record.init(this->args.record_fm2_path.c_str());
    if (!did_load)
      fprintf(stderr, "[Record][fm2] Failed to setup Movie recording!\n");
    fprintf(stderr, "[Record][fm2] Movie recording is setup!\n");
  }

  // -------------------------- NES Initialization -------------------------- //

  // pass controllers to this->fm2_record
  this->emu->fm2_record.set_joy(0, FM2_Controller::SI_GAMEPAD, &this->emu->joy_1);
  this->emu->fm2_record.set_joy(1, FM2_Controller::SI_GAMEPAD, &this->emu->joy_2);

  // Check if there is fm2 to replay
  if (this->emu->fm2_replay.is_enabled()) {
    // plug in fm2 controllers
    this->emu->nes.attach_joy(0, this->emu->fm2_replay.get_joy(0));
    this->emu->nes.attach_joy(1, this->emu->fm2_replay.get_joy(1));
  } else {
    // plug in physical nes controllers
    this->emu->nes.attach_joy(0, &this->emu->joy_1);
    this->emu->nes.attach_joy(1, &this->emu->zap_2);
  }

  // Load ROM if one has been passed as param
  if (this->args.rom != "") {
    this->menu->in_menu = false;
    int error = this->emu->load_rom(this->args.rom.c_str());
    if (error) return error;
  }

  return 0;
}

SDL_GUI::~SDL_GUI() {
  fprintf(stderr, "[SDL2] Stopping SDL2 GUI\n");

  // Cleanup ROM (unloading also creates savs)
  this->emu->unload_rom(this->emu->cart);
  delete this->emu->cart;

  // Update config
  // TODO: put this somewhere else...
  char new_roms_dir [256];
  ANESE_fs::util::get_abs_path(this->menu->menu.directory, new_roms_dir, 256);
  strcpy(this->config.roms_dir, new_roms_dir);

  this->config.save(this->args.config_file.c_str());

  delete this->menu;
  delete this->emu;

  // SDL Cleanup
  // SDL_CloseAudioDevice(this->sdl_common.nes_audiodev);
  SDL_GameControllerClose(this->sdl_common.controller);
  SDL_DestroyRenderer(this->sdl_common.renderer);
  SDL_DestroyWindow(this->sdl_common.window);
  SDL_Quit();

  SDL2_inprint::kill_inline_font();

  printf("\nANESE closed successfully\n");
}

void SDL_GUI::input_global(const SDL_Event& event) {
  if (
    (event.type == SDL_QUIT) ||
    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)
  ) this->sdl_common.running = false;

  if (event.type == SDL_KEYDOWN) {
    switch (event.key.keysym.sym) {
      case SDLK_ESCAPE:
        this->menu->in_menu = !this->menu->in_menu; break;
    }
  }

  if (event.type == SDL_CONTROLLERBUTTONDOWN ||
      event.type == SDL_CONTROLLERBUTTONUP) {
    switch (event.cbutton.button) {
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
      this->menu->in_menu = !this->menu->in_menu; break;
    }
  }
}

int SDL_GUI::run() {
  fprintf(stderr, "[SDL2] Running SDL2 GUI\n");

  double past_fups [20] = {60.0}; // more samples == less value jitter
  uint past_fups_i = 0;

  while (this->sdl_common.running) {
    typedef uint time_ms;
    time_ms frame_start_time = SDL_GetTicks();
    past_fups_i++;

    // Check for new events
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
      this->input_global(event);

      if (!this->menu->in_menu)
        this->emu->input(event);
      else
        this->menu->input(event);
    }

    // Update the NES when not in menu
    if (!this->menu->in_menu)
      this->emu->update();
    else
      this->menu->update();

    // Render something
    this->emu->output(); // keep showing NES in the background
    if (this->menu->in_menu) {
      this->menu->output();
    }

    // SHOW ME WHAT YOU GOT
    SDL_RenderPresent(this->sdl_common.renderer);

    // time how long all-that took
    time_ms frame_end_time = SDL_GetTicks();

    // ---- Count Framerate ---- //
    // Update fups for this frame
    past_fups[past_fups_i % 20] = 1000.0 / (frame_end_time - frame_start_time);

    // Get the average fups over the past 20 frames
    double avg_fps = 0;
    for(unsigned i = 0; i < 20; i++)
      avg_fps += past_fups[i];
    avg_fps /= 20;

    // Present fups though the title of the main window
    char window_title [64];
    sprintf(window_title, "anese - %u fups - %u%% speed",
      uint(avg_fps), this->emu->params.speed);
    SDL_SetWindowTitle(this->sdl_common.window, window_title);
  }

  return 0;
}
