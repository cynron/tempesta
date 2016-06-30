
const unsigned char __c_avx_data[128] __attribute__((aligned(64))) =
{
    //header charset
    0xa8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8,
    0xf8, 0xf8, 0xf0, 0x50, 0x50, 0x54, 0x50, 0x50,
    //uri charset
    0xa8, 0xf8, 0xf8, 0xfc, 0xfc, 0xfc, 0xfc, 0xf8,
    0xf8, 0xf8, 0xf4, 0x5c, 0x54, 0x5c, 0x54, 0x5c,
    //constants1
    'A','A','A','A','Z','Z','Z','Z',
    ' ',' ',' ',' ',':',':',':',':',
    //constants2
    '\t','\t','\t','\t',0,0,0,0,
    0, 0, 0, 0, 0, 0, 0, 0,
    //constants3
    0xF,0xF,0xF,0xF,0xF,0xF,0xF,0xF,
    1,  2,  4,  8,  16, 32, 64, 128,
    31 ,31 ,31 ,31 ,31 ,31 ,31 ,31 ,
    127,127,127,127,127,127,127,127,
};

const unsigned char __c_avx_method[64] __attribute__((aligned(64))) =
{
    'g','e','t', 0 ,'h','e','a','d','p','o','s','t', 0 , 0 , 0 , 0 ,
    'h','t','t','p',':','/','/', 0 ,'h','t','t','p','s',':','/','/',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

const unsigned char __c_avx_header_names[4*64] __attribute__((aligned(64))) =
{
    0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,

    'h','o','s','t', 0 , 0 , 0 , 0 ,
    't','r','a','n','s','f','e','r',
    'x','-','f','o','r','w','a','r',
    'c','o','n','t','e','n','t','-',

    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,

    'c','a','c','h','e','-','c','o',
    'c','o','o','k','i','e', 0,  0 ,
    'c','o','n','n','e','c','t','i',
    'u','s','e','r','-','a','g','e',

    0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,

    't','y','p','e', 0 , 0 , 0 , 0 ,
    '-','e','n','c','o','d','i','n',
    'd','e','d','-','f','o','r', 0 ,
    'l','e','n','g','t','h', 0 , 0 ,

    0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

    'n','t','r','o','l', 0 , 0 , 0 ,
    'g', 0 , 0 , 0 , 0 , 0 , 0 , 0 ,
    'o','n', 0 , 0 , 0 , 0 , 0 , 0 ,
    'n','t', 0 , 0 , 0 , 0 , 0 , 0 ,
};
