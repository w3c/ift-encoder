#ifndef IFT_TESTDATA_TEST_SEGMENTS_H_
#define IFT_TESTDATA_TEST_SEGMENTS_H_

#include <cstdint>

#include "common/int_set.h"

namespace ift::testdata {

static uint32_t TEST_SEGMENT_1[] = {
    104,  106,  108,  110,  114,  117,  121,  149,  158,  160,  165,  166,
    167,  168,  186,  229,  230,  231,  237,  238,  239,  240,  241,  242,
    243,  244,  245,  246,  294,  296,  298,  300,  302,  303,  304,  305,
    307,  309,  311,  313,  315,  317,  319,  323,  324,  325,  327,  328,
    330,  331,  332,  334,  335,  338,  339,  354,  355,  357,  358,  360,
    364,  365,  366,  367,  368,  371,  374,  375,  388,  389,  390,  392,
    394,  396,  397,  399,  401,  402,  405,  407,  409,  410,  411,  417,
    419,  421,  424,  425,  426,  427,  428,  429,  434,  439,  440,  441,
    448,  449,  450,  451,  453,  455,  457,  459,  460,  461,  462,  463,
    469,  477,  478,  775,  776,  781,  782,  783,  784,  785,  786,  787,
    788,  789,  790,  811,  812,  813,  826,  827,  849,  851,  853,  855,
    857,  858,  859,  860,  862,  864,  866,  868,  870,  872,  874,  878,
    879,  880,  882,  883,  885,  886,  887,  889,  890,  893,  894,  909,
    910,  912,  913,  915,  919,  920,  921,  922,  923,  926,  929,  930,
    941,  942,  943,  945,  947,  949,  950,  952,  954,  955,  958,  960,
    962,  963,  964,  970,  972,  974,  977,  978,  979,  980,  981,  982,
    987,  992,  993,  994,  1001, 1002, 1003, 1004, 1006, 1008, 1010, 1012,
    1013, 1014, 1015, 1016, 1022, 1030, 1033, 1034};

static common::GlyphSet TestSegment1() {
  common::GlyphSet result;
  for (uint32_t v : TEST_SEGMENT_1) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_SEGMENT_2[] = {
    100,  112,  159,  162,  171,  172,  175,  177,  184,  234,  235,  236,
    247,  249,  250,  257,  293,  295,  297,  299,  301,  306,  308,  310,
    312,  314,  316,  318,  320,  321,  322,  326,  329,  333,  336,  337,
    340,  341,  342,  343,  344,  345,  346,  347,  348,  349,  350,  351,
    352,  353,  356,  359,  361,  362,  363,  369,  372,  379,  380,  382,
    384,  387,  391,  393,  395,  398,  400,  403,  404,  406,  408,  412,
    413,  414,  415,  416,  418,  420,  422,  423,  430,  431,  432,  433,
    435,  436,  437,  438,  442,  443,  444,  445,  446,  447,  452,  454,
    456,  458,  465,  468,  470,  471,  472,  479,  779,  780,  801,  802,
    803,  804,  805,  806,  807,  808,  809,  810,  814,  815,  816,  817,
    818,  819,  820,  821,  822,  823,  840,  845,  848,  850,  852,  854,
    856,  861,  863,  865,  867,  869,  871,  873,  875,  876,  877,  881,
    884,  888,  891,  892,  895,  896,  897,  898,  899,  900,  901,  902,
    903,  904,  905,  906,  907,  908,  911,  914,  916,  917,  918,  924,
    927,  935,  937,  940,  944,  946,  948,  951,  953,  956,  957,  959,
    961,  965,  966,  967,  968,  969,  971,  973,  975,  976,  983,  984,
    985,  986,  988,  989,  990,  991,  995,  996,  997,  998,  999,  1000,
    1005, 1007, 1009, 1011, 1018, 1021, 1023, 1024, 1025, 1031, 1035, 1036,
    1037, 1038, 1039, 1040, 1041, 1042, 1043, 1044, 1045, 1046, 1047, 1048,
    1049, 1050, 1051, 1052, 1053, 1054, 1055};

static common::GlyphSet TestSegment2() {
  common::GlyphSet result;
  for (uint32_t v : TEST_SEGMENT_2) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_SEGMENT_3[] = {169};

static common::GlyphSet TestSegment3() {
  common::GlyphSet result;
  for (uint32_t v : TEST_SEGMENT_3) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_SEGMENT_4[] = {
    96,   97,   98,   99,  101, 102, 103, 105, 107, 109, 111,  113,  115,  116,
    118,  119,  120,  122, 123, 124, 125, 126, 127, 128, 129,  130,  131,  132,
    133,  134,  135,  136, 137, 138, 139, 140, 141, 142, 143,  144,  145,  146,
    147,  148,  150,  151, 152, 153, 154, 155, 156, 157, 161,  163,  164,  170,
    173,  174,  176,  178, 179, 180, 181, 182, 183, 185, 187,  188,  189,  190,
    191,  192,  193,  194, 195, 196, 197, 198, 199, 200, 201,  202,  203,  204,
    205,  206,  207,  208, 209, 210, 211, 212, 213, 214, 215,  216,  217,  218,
    219,  220,  221,  222, 223, 224, 225, 226, 227, 228, 232,  233,  248,  251,
    252,  253,  254,  255, 256, 258, 259, 260, 261, 262, 263,  264,  265,  266,
    267,  268,  269,  270, 271, 272, 273, 274, 275, 276, 277,  278,  279,  280,
    281,  282,  283,  284, 285, 286, 287, 288, 289, 290, 291,  292,  370,  373,
    376,  377,  378,  381, 383, 385, 386, 464, 466, 467, 473,  474,  475,  476,
    480,  481,  482,  483, 484, 485, 486, 487, 488, 489, 490,  491,  492,  493,
    494,  495,  496,  497, 498, 499, 500, 501, 502, 503, 504,  505,  506,  507,
    508,  509,  510,  511, 512, 513, 514, 515, 516, 517, 518,  519,  520,  521,
    522,  523,  524,  525, 526, 527, 528, 529, 530, 531, 532,  533,  534,  535,
    536,  537,  538,  539, 540, 541, 542, 543, 544, 545, 546,  547,  548,  549,
    550,  551,  552,  553, 554, 555, 556, 557, 558, 559, 560,  561,  562,  563,
    564,  565,  566,  567, 568, 569, 570, 571, 572, 573, 574,  575,  576,  577,
    578,  579,  580,  581, 582, 583, 584, 585, 586, 587, 588,  589,  590,  591,
    592,  593,  594,  595, 596, 597, 598, 599, 600, 601, 602,  603,  604,  605,
    606,  607,  608,  609, 610, 611, 612, 613, 614, 615, 616,  617,  618,  619,
    620,  621,  622,  623, 624, 625, 626, 627, 628, 629, 630,  631,  632,  633,
    634,  635,  636,  637, 638, 639, 640, 641, 642, 643, 644,  645,  646,  647,
    648,  649,  650,  651, 652, 653, 654, 655, 656, 657, 658,  659,  660,  661,
    662,  663,  664,  665, 666, 667, 668, 669, 670, 671, 672,  673,  674,  675,
    676,  677,  678,  679, 680, 681, 682, 683, 684, 685, 686,  687,  688,  689,
    690,  691,  692,  693, 694, 695, 696, 697, 698, 699, 700,  701,  702,  703,
    704,  705,  706,  707, 708, 709, 710, 711, 712, 713, 714,  715,  716,  717,
    718,  719,  720,  721, 722, 723, 724, 725, 726, 727, 728,  729,  730,  731,
    732,  733,  734,  735, 736, 737, 738, 739, 740, 741, 742,  743,  744,  745,
    746,  747,  748,  749, 750, 751, 752, 753, 754, 755, 756,  757,  758,  759,
    760,  761,  762,  763, 764, 765, 766, 767, 768, 769, 777,  778,  791,  792,
    793,  794,  795,  796, 797, 798, 799, 800, 838, 841, 842,  843,  844,  846,
    847,  925,  928,  931, 932, 933, 934, 936, 938, 939, 1017, 1019, 1020, 1026,
    1027, 1028, 1029, 1032};

static common::GlyphSet TestSegment4() {
  common::GlyphSet result;
  for (uint32_t v : TEST_SEGMENT_4) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_VF_SEGMENT_1[] = {
    104,  106,  108,  110,  114,  117,  121,  149,  158,  160,  165,  166,
    167,  168,  186,  229,  230,  231,  237,  238,  239,  240,  241,  242,
    243,  244,  245,  246,  294,  296,  298,  300,  302,  303,  304,  305,
    307,  309,  311,  313,  315,  317,  319,  323,  324,  325,  327,  328,
    330,  331,  332,  334,  335,  338,  339,  354,  355,  357,  358,  360,
    364,  365,  366,  367,  368,  371,  374,  375,  388,  389,  390,  392,
    394,  396,  397,  399,  401,  402,  405,  407,  409,  410,  411,  417,
    419,  421,  424,  425,  426,  427,  428,  429,  434,  439,  440,  441,
    448,  449,  450,  451,  453,  455,  457,  459,  460,  461,  462,  463,
    469,  477,  478,  775,  776,  781,  782,  783,  784,  785,  786,  787,
    788,  789,  790,  826,  827,  849,  851,  853,  855,  857,  858,  859,
    860,  862,  864,  866,  868,  870,  872,  874,  878,  879,  880,  882,
    883,  885,  886,  887,  889,  890,  893,  894,  909,  910,  912,  913,
    915,  919,  920,  921,  922,  923,  926,  929,  930,  941,  942,  943,
    945,  947,  949,  950,  952,  954,  955,  958,  960,  962,  963,  964,
    970,  972,  974,  977,  978,  979,  980,  981,  982,  987,  992,  993,
    994,  1001, 1002, 1003, 1004, 1006, 1008, 1010, 1012, 1013, 1014, 1015,
    1016, 1022, 1030};

static common::GlyphSet TestVfSegment1() {
  common::GlyphSet result;
  for (uint32_t v : TEST_VF_SEGMENT_1) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_VF_SEGMENT_2[] = {
    100,  112,  159,  162,  171,  172,  175,  177,  184,  234,  235,  236,
    247,  249,  250,  257,  293,  295,  297,  299,  301,  306,  308,  310,
    312,  314,  316,  318,  320,  321,  322,  326,  329,  333,  336,  337,
    340,  341,  342,  343,  344,  345,  346,  347,  348,  349,  350,  351,
    352,  353,  356,  359,  361,  362,  363,  369,  372,  379,  380,  382,
    384,  387,  391,  393,  395,  398,  400,  403,  404,  406,  408,  412,
    413,  414,  415,  416,  418,  420,  422,  423,  430,  431,  432,  433,
    435,  436,  437,  438,  442,  443,  444,  445,  446,  447,  452,  454,
    456,  458,  465,  468,  470,  471,  472,  479,  779,  780,  801,  802,
    803,  804,  805,  806,  807,  808,  809,  810,  811,  812,  813,  814,
    815,  816,  817,  818,  819,  820,  821,  822,  823,  840,  845,  848,
    850,  852,  854,  856,  861,  863,  865,  867,  869,  871,  873,  875,
    876,  877,  881,  884,  888,  891,  892,  895,  896,  897,  898,  899,
    900,  901,  902,  903,  904,  905,  906,  907,  908,  911,  914,  916,
    917,  918,  924,  927,  935,  937,  940,  944,  946,  948,  951,  953,
    956,  957,  959,  961,  965,  966,  967,  968,  969,  971,  973,  975,
    976,  983,  984,  985,  986,  988,  989,  990,  991,  995,  996,  997,
    998,  999,  1000, 1005, 1007, 1009, 1011, 1018, 1021, 1023, 1024, 1025,
    1031, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041, 1042, 1043,
    1044, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052, 1053, 1054, 1055,
};

static common::GlyphSet TestVfSegment2() {
  common::GlyphSet result;
  for (uint32_t v : TEST_VF_SEGMENT_2) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_VF_SEGMENT_3[] = {169};

static common::GlyphSet TestVfSegment3() {
  common::GlyphSet result;
  for (uint32_t v : TEST_VF_SEGMENT_3) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_VF_SEGMENT_4[] = {
    96,   97,   98,   102,  103,  107,  109, 111, 113, 115, 116, 118, 120, 122,
    125,  126,  127,  128,  129,  130,  131, 133, 134, 135, 139, 140, 146, 148,
    150,  152,  154,  155,  157,  161,  163, 164, 170, 173, 176, 178, 179, 180,
    181,  182,  183,  185,  187,  190,  191, 192, 217, 218, 219, 220, 221, 222,
    223,  224,  225,  226,  227,  228,  232, 233, 248, 251, 252, 253, 254, 255,
    256,  258,  259,  260,  261,  262,  263, 264, 265, 266, 267, 268, 269, 270,
    271,  272,  273,  274,  275,  276,  277, 278, 279, 280, 281, 282, 283, 284,
    285,  286,  287,  288,  289,  290,  291, 292, 370, 373, 376, 377, 378, 381,
    383,  385,  386,  464,  466,  467,  473, 474, 475, 476, 480, 481, 484, 490,
    491,  496,  533,  534,  536,  537,  538, 539, 540, 542, 543, 544, 545, 546,
    547,  548,  549,  550,  552,  553,  554, 556, 557, 558, 560, 561, 563, 565,
    566,  567,  568,  570,  571,  572,  573, 574, 575, 576, 577, 579, 580, 582,
    583,  584,  586,  587,  589,  591,  592, 593, 594, 595, 596, 597, 598, 600,
    601,  602,  603,  604,  605,  606,  607, 608, 609, 610, 611, 612, 613, 614,
    615,  616,  617,  618,  620,  621,  622, 623, 625, 626, 627, 630, 631, 634,
    635,  636,  638,  639,  640,  641,  643, 644, 645, 646, 647, 648, 649, 650,
    651,  652,  653,  655,  657,  658,  659, 660, 662, 663, 665, 667, 669, 671,
    672,  673,  674,  675,  677,  678,  679, 680, 681, 683, 684, 685, 688, 689,
    690,  691,  692,  694,  697,  699,  701, 702, 703, 704, 706, 707, 708, 709,
    711,  712,  713,  715,  717,  719,  722, 723, 724, 726, 728, 729, 730, 731,
    732,  733,  735,  737,  739,  740,  741, 743, 744, 746, 747, 748, 749, 751,
    752,  753,  754,  755,  757,  759,  761, 764, 765, 777, 778, 838, 841, 842,
    843,  844,  846,  847,  925,  928,  931, 932, 933, 934, 936, 938, 939, 1017,
    1019, 1020, 1026, 1027, 1028, 1029, 1032};

static common::GlyphSet TestVfSegment4() {
  common::GlyphSet result;
  for (uint32_t v : TEST_VF_SEGMENT_4) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_1[] = {
    104, 106, 108, 110, 114, 117, 121, 149, 158, 160, 165, 166, 167, 168, 186,
    229, 230, 231, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 294, 296,
    298, 300, 302, 303, 304, 305, 307, 309, 311, 313, 315, 317, 319, 323, 324,
    325, 327, 328, 330, 331, 332, 334, 335, 338, 339, 354, 355, 357, 358, 360,
    364, 365, 366, 367, 368, 371, 374, 375, 388, 389, 390, 392, 394, 396, 397,
    399, 401, 402, 405, 407, 409, 410, 411, 417, 419, 421, 424, 425, 426, 427,
    428, 429, 434, 439, 440, 441, 448, 449, 450, 451, 453, 455, 457, 459, 460,
    461, 462, 463, 469, 477, 478, 801, 802, 803, 804, 805, 806, 807, 808, 809,
    810, 811, 812, 813, 814, 815, 817, 822, 826, 827};

static common::GlyphSet TestFeatureSegment1() {
  common::GlyphSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_1) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_2[] = {
    100, 112, 159, 162, 171, 172, 175, 177, 184, 234, 235, 236, 247, 249,
    250, 257, 293, 295, 297, 299, 301, 306, 308, 310, 312, 314, 316, 318,
    320, 321, 322, 326, 329, 333, 336, 337, 340, 341, 342, 343, 344, 345,
    346, 347, 348, 349, 350, 351, 352, 353, 356, 359, 361, 362, 363, 369,
    372, 379, 380, 382, 384, 387, 391, 393, 395, 398, 400, 403, 404, 406,
    408, 412, 413, 414, 415, 416, 418, 420, 422, 423, 430, 431, 432, 433,
    435, 436, 437, 438, 442, 443, 444, 445, 446, 447, 452, 454, 456, 458,
    465, 468, 470, 471, 472, 479, 816, 818, 819, 820, 821, 823,
};

static common::GlyphSet TestFeatureSegment2() {
  common::GlyphSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_2) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_3[] = {169};

static common::GlyphSet TestFeatureSegment3() {
  common::GlyphSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_3) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_4[] = {
    96,  97,  98,  99,  101, 102, 103, 105, 107, 109, 111, 113, 115, 116, 118,
    119, 120, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 150,
    151, 152, 153, 154, 155, 156, 157, 161, 163, 164, 170, 173, 174, 176, 178,
    179, 180, 181, 182, 183, 185, 187, 188, 189, 190, 191, 192, 193, 194, 195,
    196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210,
    211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    226, 227, 228, 232, 233, 248, 251, 252, 253, 254, 255, 256, 258, 259, 260,
    261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275,
    276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290,
    291, 292, 370, 373, 376, 377, 378, 381, 383, 385, 386, 464, 466, 467, 473,
    474, 475, 476, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491,
    492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506,
    507, 508, 509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521,
    522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536,
    537, 538, 539, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551,
    552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566,
    567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581,
    582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596,
    597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611,
    612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626,
    627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641,
    642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656,
    657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671,
    672, 673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686,
    687, 688, 689, 690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701,
    702, 703, 704, 705, 706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716,
    717, 718, 719, 720, 721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731,
    732, 733, 734, 735, 736, 737, 738, 739, 740, 741, 742, 743, 744, 745, 746,
    747, 748, 749, 750, 751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761,
    762, 763, 764, 765, 766, 767, 768, 769, 791, 792, 793, 794, 795, 796, 797,
    798, 799, 800};

static common::GlyphSet TestFeatureSegment4() {
  common::GlyphSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_4) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_5[] = {
    775,  776,  779,  780,  781,  782,  783,  784,  785,  786,  787,  788,
    789,  790,  840,  845,  848,  849,  850,  851,  852,  853,  854,  855,
    856,  857,  858,  859,  860,  861,  862,  863,  864,  865,  866,  867,
    868,  869,  870,  871,  872,  873,  874,  875,  876,  877,  878,  879,
    880,  881,  882,  883,  884,  885,  886,  887,  888,  889,  890,  891,
    892,  893,  894,  895,  896,  897,  898,  899,  900,  901,  902,  903,
    904,  905,  906,  907,  908,  909,  910,  911,  912,  913,  914,  915,
    916,  917,  918,  919,  920,  921,  922,  923,  924,  926,  927,  929,
    930,  935,  937,  940,  941,  942,  943,  944,  945,  946,  947,  948,
    949,  950,  951,  952,  953,  954,  955,  956,  957,  958,  959,  960,
    961,  962,  963,  964,  965,  966,  967,  968,  969,  970,  971,  972,
    973,  974,  975,  976,  977,  978,  979,  980,  981,  982,  983,  984,
    985,  986,  987,  988,  989,  990,  991,  992,  993,  994,  995,  996,
    997,  998,  999,  1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008,
    1009, 1010, 1011, 1012, 1013, 1014, 1015, 1016, 1018, 1021, 1022, 1023,
    1024, 1025, 1030, 1031, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 1040,
    1041, 1042, 1043, 1044, 1045, 1046, 1047, 1048, 1049, 1050, 1051, 1052,
    1053, 1054, 1055};

static common::IntSet TestFeatureSegment5() {
  common::IntSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_5) {
    result.insert(v);
  }
  return result;
}

static uint32_t TEST_FEATURE_SEGMENT_6[] = {
    777, 778, 838, 839, 841, 842,  843,  844,  846,  847,  925,  928,  931, 932,
    933, 934, 936, 938, 939, 1017, 1019, 1020, 1026, 1027, 1028, 1029, 1032};

static common::GlyphSet TestFeatureSegment6() {
  common::GlyphSet result;
  for (uint32_t v : TEST_FEATURE_SEGMENT_6) {
    result.insert(v);
  }
  return result;
}

}  // namespace ift::testdata

#endif  // IFT_TESTDATA_TEST_SEGMENTS_H_