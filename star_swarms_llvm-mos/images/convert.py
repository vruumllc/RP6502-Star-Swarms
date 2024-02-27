#!/usr/bin/python3

from PIL import Image


def rp6502_rgb_tile_bpp4(r1,g1,b1,r2,g2,b2):
    return (((b1>>7)<<6)|((g1>>7)<<5)|((r1>>7)<<4)|((b2>>7)<<2)|((g2>>7)<<1)|(r2>>7))

def conv_tile(name_in, size, name_out):
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size,size])
            for y in range(0, im2.height):
                for x in range(0, im2.width, 2):
                    r1, g1, b1 = im2.getpixel((x, y))
                    r2, g2, b2 = im2.getpixel((x+1, y))
                    o.write(
                        rp6502_rgb_tile_bpp4(r1,g1,b1,r2,g2,b2).to_bytes(
                            1, byteorder="little", signed=False
                        )
                    )

def rp6502_rgb_sprite_bpp16(r,g,b):
    if r==0 and g==0 and b==0:
        return 0
    else:
        return ((((b>>3)<<11)|((g>>3)<<6)|(r>>3))|1<<5)

def conv_spr(name_in, size, name_out):
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size,size])
            for y in range(0, im2.height):
                for x in range(0, im2.width):
                    r, g, b = im2.getpixel((x, y))
                    o.write(
                        rp6502_rgb_sprite_bpp16(r,g,b).to_bytes(
                            2, byteorder="little", signed=False
                        )
                    )

conv_tile("space0.png", 16, "space0.bin")
conv_tile("space1.png", 16, "space1.bin")
conv_tile("space2.png", 16, "space2.bin")
conv_tile("space3.png", 16, "space3.bin")
conv_tile("space4.png", 16, "space4.bin")
conv_tile("space5.png", 16, "space5.bin")
conv_tile("space6.png", 16, "space6.bin")
conv_tile("space7.png", 16, "space7.bin")
conv_tile("space8.png", 16, "space8.bin")
conv_tile("space9.png", 16, "space9.bin")
conv_tile("space10.png", 16, "space10.bin")
conv_tile("space11.png", 16, "space11.bin")
conv_tile("space12.png", 16, "space12.bin")
conv_tile("space13.png", 16, "space13.bin")
conv_tile("space14.png", 16, "space14.bin")
conv_tile("space15.png", 16, "space15.bin")

conv_spr("alien_blue.png", 16, "alien_blue.bin")
conv_spr("alien_green.png", 16, "alien_green.bin")
conv_spr("alien_yellow.png", 16, "alien_yellow.bin")
conv_spr("alien_pink.png", 16, "alien_pink.bin")
conv_spr("alien_red.png", 16, "alien_red.bin")
conv_spr("alien_white.png", 16, "alien_white.bin")
conv_spr("alien_missle.png", 4, "alien_missle.bin")
conv_spr("missle.png", 4, "missle.bin")
conv_spr("spaceship.png", 16, "spaceship.bin")
conv_spr("explosion.png", 32, "explosion.bin")
