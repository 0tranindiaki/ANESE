#pragma once

#include <istream>

#include "common/util.h"

// iNES file container
// https://wiki.nesdev.com/w/index.php/INES
struct INES {
private:
  // Raw ROM data
  const u8* raw_data;
        u32 data_len;

public:
  u8 mapper; // Mapper number

  bool is_valid;

  // ROMs
  struct {
    const u8* prg_rom; // start of prg_rom banks (16K each)
    const u8* chr_rom; // start of chr_rom banks (8k each)

    const u8* trn_rom; // start of Trainer
    const u8* pci_rom; // start of PlayChoice INST-ROM
    const u8* pc_prom; // start PlayChoice PROM
  } roms;

  struct {
    u8 prg_rom_pages; // Num 16K program ROM pages
    u8 chr_rom_pages; // Num 8K character ROM pages

    bool mirror_type; // 0 = horizontal mirroring, 1 = vertical mirroring
    bool has_4screen; // Uses Four Screen Mode
    bool has_trainer; // Has 512 byte trainer at $7000 - $71FFh
    bool has_battery; // Has battery backed SRAM at $6000 - $7FFFh

    bool is_PC10; // is a PC-10 Game
    bool is_VS;   // is a VS. Game

    bool is_NES2; // is using NES 2.0 extensions
  } flags;

  ~INES();
  INES(const u8* data, u32 data_len);
};