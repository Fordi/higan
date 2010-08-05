#ifdef SPPU_CPP

#include "list.cpp"

void sPPU::Sprite::address_reset() {
  self.regs.oam_addr = self.regs.oam_baseaddr << 1;
  regs.first_sprite = (self.regs.oam_priority == false ? 0 : (self.regs.oam_addr >> 2) & 127);
}

void sPPU::Sprite::frame() {
  regs.time_over = false;
  regs.range_over = false;
}

void sPPU::Sprite::scanline() {
  t.x = 0;
  t.y = self.vcounter();

  if(t.y == (!self.regs.overscan ? 225 : 240) && self.regs.display_disabled == false) address_reset();
  if(t.y > (!self.regs.overscan ? 224 : 239)) return;

  t.item_count = 0;
  t.tile_count = 0;

  t.active = !t.active;
  auto oam_item = t.item[t.active];
  auto oam_tile = t.tile[t.active];

  memset(oam_item, 0xff, 32);
  for(unsigned i = 0; i < 34; i++) oam_tile[i].tile = 0xffff;

  for(unsigned i = 0; i < 128; i++) {
    t.active_sprite = (i + regs.first_sprite) & 127;
    if(on_scanline() == false) continue;
    if(t.item_count++ >= 32) break;
    oam_item[t.item_count - 1] = (i + regs.first_sprite) & 127;
  }

  if(t.item_count > 0 && oam_item[t.item_count - 1] != 0xff) {
    ppu.regs.ioamaddr = 0x0200 + (oam_item[t.item_count - 1] >> 2);
  }

  for(signed i = 31; i >= 0; i--) {
    if(oam_item[i] == 0xff) continue;
    t.active_sprite = oam_item[i];
    load_tiles();
  }

  regs.time_over  |= (t.tile_count > 34);
  regs.range_over |= (t.item_count > 32);
}

bool sPPU::Sprite::on_scanline() {
  SpriteItem &sprite = list[t.active_sprite];
  if(sprite.x > 256 && (sprite.x + sprite.width() - 1) < 512) return false;

  signed height = (regs.interlace == false ? sprite.height() : (sprite.height() >> 1));
  if(t.y >= sprite.y && t.y < (sprite.y + height)) return true;
  if((sprite.y + height) >= 256 && t.y < ((sprite.y + height) & 255)) return true;
  return false;
}

void sPPU::Sprite::load_tiles() {
  SpriteItem &sprite = list[t.active_sprite];
  unsigned tile_width = sprite.width() >> 3;
  signed x = sprite.x;
  signed y = (t.y - sprite.y) & 0xff;
  if(regs.interlace) y <<= 1;

  if(sprite.vflip) {
    if(sprite.width() == sprite.height()) {
      y = (sprite.height() - 1) - y;
    } else {
      y = (y < sprite.width()) ? ((sprite.width() - 1) - y) : (sprite.width() + ((sprite.width() - 1) - (y - sprite.width())));
    }
  }

  if(regs.interlace) {
    y = (sprite.vflip == false ? y + self.field() : y - self.field());
  }

  x &= 511;
  y &= 255;

  uint16 tiledata_addr = regs.tiledata_addr;
  uint16 chrx = (sprite.character >> 0) & 15;
  uint16 chry = (sprite.character >> 4) & 15;
  if(sprite.nameselect) {
    tiledata_addr += (256 * 32) + (regs.nameselect << 13);
  }
  chry  += (y >> 3);
  chry  &= 15;
  chry <<= 4;

  auto oam_tile = t.tile[t.active];

  for(unsigned tx = 0; tx < tile_width; tx++) {
    unsigned sx = (x + (tx << 3)) & 511;
    if(x != 256 && sx >= 256 && (sx + 7) < 512) continue;

    if(t.tile_count++ >= 34) break;
    unsigned n = t.tile_count - 1;
    oam_tile[n].x = sx;
    oam_tile[n].y = y;
    oam_tile[n].priority = sprite.priority;
    oam_tile[n].palette = 128 + (sprite.palette << 4);
    oam_tile[n].hflip = sprite.hflip;

    unsigned mx = (sprite.hflip == false) ? tx : ((tile_width - 1) - tx);
    unsigned pos = tiledata_addr + ((chry + ((chrx + mx) & 15)) << 5);
    oam_tile[n].tile = (pos >> 5) & 0x07ff;
  }
}

void sPPU::Sprite::run() {
  output.main.priority = 0;
  output.sub.priority = 0;

  unsigned priority_table[] = { regs.priority0, regs.priority1, regs.priority2, regs.priority3 };
  unsigned x = t.x++;

  for(unsigned n = 0; n < 34; n++) {
    TileItem &item = t.tile[!t.active][n];
    if(item.tile == 0xffff) break;

    int px = x - sclip<9>(item.x);
    if(px & ~7) continue;

    uint16 addr = (item.tile << 5) + ((item.y & 7) * 2);
    unsigned mask = 0x80 >> (item.hflip == false ? px : 7 - px);

    uint8 d0 = memory::vram[addr +  0];
    uint8 d1 = memory::vram[addr +  1];
    uint8 d2 = memory::vram[addr + 16];
    uint8 d3 = memory::vram[addr + 17];

    unsigned color;
    color  = ((bool)(d0 & mask)) << 0;
    color |= ((bool)(d1 & mask)) << 1;
    color |= ((bool)(d2 & mask)) << 2;
    color |= ((bool)(d3 & mask)) << 3;

    if(color) {
      color += item.palette;

      if(regs.main_enabled) {
        output.main.palette = color;
        output.main.priority = priority_table[item.priority];
      }

      if(regs.sub_enabled) {
        output.sub.palette = color;
        output.sub.priority = priority_table[item.priority];
      }
    }
  }
}

void sPPU::Sprite::reset() {
  for(unsigned i = 0; i < 128; i++) {
    list[i].x = 0;
    list[i].y = 0;
    list[i].character = 0;
    list[i].nameselect = 0;
    list[i].vflip = 0;
    list[i].hflip = 0;
    list[i].priority = 0;
    list[i].palette = 0;
    list[i].size = 0;
  }

  t.x = 0;
  t.y = 0;

  t.active_sprite = 0;
  t.item_count = 0;
  t.tile_count = 0;

  t.active = 0;
  for(unsigned n = 0; n < 2; n++) {
    memset(t.item[n], 0, 32);
    for(unsigned i = 0; i < 34; i++) {
      t.tile[n][i].x = 0;
      t.tile[n][i].y = 0;
      t.tile[n][i].priority = 0;
      t.tile[n][i].palette = 0;
      t.tile[n][i].tile = 0;
      t.tile[n][i].hflip = 0;
    }
  }

  regs.main_enabled = 0;
  regs.sub_enabled = 0;
  regs.interlace = 0;

  regs.base_size = 0;
  regs.nameselect = 0;
  regs.tiledata_addr = 0;
  regs.first_sprite = 0;

  regs.priority0 = 0;
  regs.priority1 = 0;
  regs.priority2 = 0;
  regs.priority3 = 0;

  regs.time_over = 0;
  regs.range_over = 0;

  output.main.palette = 0;
  output.main.priority = 0;
  output.sub.palette = 0;
  output.sub.priority = 0;
}

sPPU::Sprite::Sprite(sPPU &self) : self(self) {
}

#endif
