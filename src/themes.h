#pragma once
#include <windows.h>

struct Theme {
    const wchar_t* name;
    COLORREF bgTop;
    COLORREF bgBottom;
    COLORREF waterSurface;
    COLORREF waterFoam;
    COLORREF lightRay;
    COLORREF sand;
    COLORREF sandDark;
    COLORREF rock;
    COLORREF rockLight;
    COLORREF coral1;
    COLORREF coral2;
    COLORREF coral3;
    COLORREF seaweedDark;
    COLORREF seaweedLight;
    COLORREF bubble;
    COLORREF food;
    COLORREF ripple;
    COLORREF crab;
    COLORREF jellyfish;
    COLORREF jellyfishGlow;
    COLORREF turtleShell;
    COLORREF turtleBody;
    COLORREF chestWood;
    COLORREF chestGold;
    COLORREF fishEye;
    COLORREF fishColors[6];
};

inline constexpr int THEME_COUNT = 5;

inline const Theme THEMES[THEME_COUNT] = {
    // 0: Deep Ocean
    {
        L"Глибокий океан",
        RGB(5, 18, 36),        // bgTop
        RGB(2, 9, 20),         // bgBottom
        RGB(64, 190, 240),     // waterSurface
        RGB(165, 235, 255),    // waterFoam
        RGB(18, 52, 82),       // lightRay
        RGB(182, 158, 112),    // sand
        RGB(118, 100, 70),     // sandDark
        RGB(86, 100, 116),     // rock
        RGB(128, 144, 162),    // rockLight
        RGB(242, 98, 130),     // coral1
        RGB(245, 162, 78),     // coral2
        RGB(188, 115, 245),    // coral3
        RGB(26, 132, 88),      // seaweedDark
        RGB(64, 202, 132),     // seaweedLight
        RGB(155, 228, 255),    // bubble
        RGB(255, 224, 115),    // food
        RGB(115, 210, 250),    // ripple
        RGB(240, 88, 66),      // crab
        RGB(188, 138, 255),    // jellyfish
        RGB(230, 198, 255),    // jellyfishGlow
        RGB(72, 178, 118),     // turtleShell
        RGB(118, 212, 152),    // turtleBody
        RGB(158, 106, 62),     // chestWood
        RGB(255, 212, 82),     // chestGold
        RGB(255, 255, 255),    // fishEye
        {
            RGB(255, 154, 58),  // Clownfish orange
            RGB(68, 222, 245),  // Tropical cyan
            RGB(255, 218, 72),  // Tang yellow
            RGB(255, 112, 160), // Coral pink
            RGB(118, 238, 156), // Mint green
            RGB(215, 234, 255)  // Silver minnow
        }
    },
    // 1: Matrix Terminal
    {
        L"Матриця (зелений)",
        RGB(2, 10, 4),
        RGB(1, 5, 2),
        RGB(55, 235, 115),
        RGB(155, 255, 185),
        RGB(10, 38, 18),
        RGB(32, 110, 56),
        RGB(18, 66, 34),
        RGB(34, 122, 64),
        RGB(62, 165, 96),
        RGB(92, 240, 138),
        RGB(48, 192, 94),
        RGB(130, 250, 165),
        RGB(22, 120, 56),
        RGB(70, 224, 118),
        RGB(145, 255, 180),
        RGB(215, 255, 160),
        RGB(90, 245, 140),
        RGB(135, 255, 168),
        RGB(72, 230, 154),
        RGB(165, 255, 205),
        RGB(48, 198, 96),
        RGB(102, 242, 142),
        RGB(38, 138, 68),
        RGB(175, 255, 180),
        RGB(235, 255, 235),
        {
            RGB(82, 255, 128),
            RGB(46, 218, 98),
            RGB(150, 255, 175),
            RGB(62, 238, 148),
            RGB(115, 248, 118),
            RGB(185, 255, 200)
        }
    },
    // 2: Retro Amber CRT
    {
        L"Ретро-бурштин",
        RGB(15, 9, 3),
        RGB(7, 4, 2),
        RGB(255, 188, 68),
        RGB(255, 228, 148),
        RGB(44, 28, 10),
        RGB(156, 104, 38),
        RGB(98, 64, 22),
        RGB(124, 84, 36),
        RGB(178, 124, 56),
        RGB(255, 146, 52),
        RGB(238, 178, 76),
        RGB(255, 206, 108),
        RGB(132, 88, 28),
        RGB(204, 144, 48),
        RGB(255, 220, 132),
        RGB(255, 244, 190),
        RGB(255, 198, 88),
        RGB(255, 126, 46),
        RGB(248, 196, 115),
        RGB(255, 232, 172),
        RGB(205, 142, 46),
        RGB(245, 184, 78),
        RGB(150, 96, 36),
        RGB(255, 222, 110),
        RGB(255, 248, 220),
        {
            RGB(255, 196, 68),
            RGB(255, 155, 48),
            RGB(255, 226, 125),
            RGB(234, 148, 56),
            RGB(248, 180, 84),
            RGB(255, 236, 168)
        }
    },
    // 3: Neon Cyberpunk
    {
        L"Неон / Кіберпанк",
        RGB(12, 6, 26),
        RGB(5, 3, 14),
        RGB(0, 245, 255),
        RGB(185, 252, 255),
        RGB(34, 15, 62),
        RGB(94, 58, 142),
        RGB(56, 32, 90),
        RGB(78, 66, 122),
        RGB(126, 108, 178),
        RGB(255, 48, 168),
        RGB(255, 198, 0),
        RGB(0, 238, 220),
        RGB(18, 142, 164),
        RGB(0, 242, 215),
        RGB(155, 250, 255),
        RGB(255, 238, 88),
        RGB(255, 82, 214),
        RGB(255, 66, 148),
        RGB(198, 92, 255),
        RGB(235, 178, 255),
        RGB(0, 215, 195),
        RGB(108, 252, 228),
        RGB(118, 62, 155),
        RGB(255, 216, 48),
        RGB(255, 255, 255),
        {
            RGB(255, 58, 178),
            RGB(0, 245, 255),
            RGB(255, 222, 42),
            RGB(184, 98, 255),
            RGB(68, 255, 188),
            RGB(255, 138, 86)
        }
    },
    // 4: Monochrome Minimal
    {
        L"Монохром",
        RGB(11, 12, 14),
        RGB(6, 7, 9),
        RGB(202, 208, 218),
        RGB(248, 250, 255),
        RGB(28, 31, 36),
        RGB(120, 124, 132),
        RGB(76, 80, 86),
        RGB(98, 102, 110),
        RGB(148, 152, 160),
        RGB(182, 186, 194),
        RGB(150, 154, 162),
        RGB(208, 212, 220),
        RGB(92, 96, 102),
        RGB(154, 158, 166),
        RGB(228, 232, 242),
        RGB(255, 255, 255),
        RGB(188, 192, 202),
        RGB(220, 224, 232),
        RGB(190, 195, 208),
        RGB(238, 242, 250),
        RGB(165, 170, 178),
        RGB(210, 215, 224),
        RGB(132, 136, 144),
        RGB(235, 238, 245),
        RGB(255, 255, 255),
        {
            RGB(246, 246, 252),
            RGB(214, 218, 226),
            RGB(184, 188, 196),
            RGB(232, 235, 242),
            RGB(198, 202, 212),
            RGB(168, 172, 180)
        }
    }
};
