#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <random>
#include "themes.h"

struct Cell {
    wchar_t ch = L' ';
    COLORREF color = RGB(255, 255, 255);
};

enum class FishSpecies {
    Minnow,
    Clownfish,
    Tropical,
    Puffer,
    Angel,
    ReefBig
};

struct Fish {
    FishSpecies species;
    float x, y;
    float vx, vy;
    float baseSpeed;
    int facing;          // +1 right, -1 left
    int colorIndex;
    int variant;
    float phase;
    float scaredTimer;
    float hungryCooldown;
    float swimTargetY;
    float turnTimer;
};

struct SeaTurtle {
    float x, y;
    float vx, vy;
    int facing;
    float phase;
};

struct Seahorse {
    float x, y;
    float baseX, baseY;
    float phase;
    int facing;
    int colorIndex;
};

struct Jellyfish {
    float x, y;
    float vx, vy;
    float phase;
    float pulseSpeed;
};

struct Seaweed {
    int baseX;
    int height;
    float phase;
    float speed;
    float bend;
    bool bright;
    bool leafy;
};

struct Bubble {
    float x, y;
    float vy;
    float phase;
    int size;
    bool popping;
    float popTimer;
    bool golden;
};

struct FoodPellet {
    float x, y;
    float vy;
    float phase;
    float life;
    wchar_t ch;
};

struct Ripple {
    float x, y;
    float radius;
    float maxRadius;
    float life;
};

struct Sparkle {
    float x, y;
    float life;
    wchar_t ch;
};

struct Coral {
    int x;
    int height;
    int style;
    int colorSlot;
};

struct Rock {
    int x;
    int width;
    int height;
};

struct ShellDecor {
    int x;
    int dy;
    wchar_t ch;
    bool star;
};

struct TreasureChest {
    int x;
    float openTimer;
    float nextOpenIn;
};

struct Crab {
    float x;
    float vx;
    float pauseTimer;
    float animPhase;
    float startledTimer;
};

class Aquarium {
public:
    Aquarium();

    void Resize(int cols, int rows, int bottomMarginRows = 3);
    void SetTheme(int themeIdx);
    int GetTheme() const { return themeIndex_; }

    void Update(float dt);
    void Render();

    // Interactions (coordinates in grid cell space)
    void OnMouseMove(float col, float row, bool active);
    void DropFood(float col, float row, int count = 6);
    void DropFoodFromSurface(int count = 14);

    int Cols() const { return cols_; }
    int Rows() const { return rows_; }
    const std::vector<Cell>& Buffer() const { return grid_; }
    const Theme& CurrentTheme() const { return THEMES[themeIndex_]; }

private:
    void Populate();
    void ClearGrid();
    void PutChar(int x, int y, wchar_t ch, COLORREF color);
    void PutString(int x, int y, const wchar_t* str, COLORREF color);
    void PutFishLine(int x, int y, const wchar_t* str, COLORREF bodyCol, COLORREF accentCol, COLORREF eyeCol);
    int FloorYAt(int x) const;
    int VisibleBottom() const { return std::max(12, rows_ - bottomMarginRows_); }

    void UpdateSeaweed(float dt);
    void UpdateFish(float dt);
    void UpdateTurtle(float dt);
    void UpdateSeahorses(float dt);
    void UpdateJellyfish(float dt);
    void UpdateCrab(float dt);
    void UpdateChest(float dt);
    void UpdateBubbles(float dt);
    void UpdateFood(float dt);
    void UpdateRipples(float dt);
    void UpdateSparkles(float dt);

    void DrawLightRays();
    void DrawSurface();
    void DrawSeabed();
    void DrawTreasureChest();
    void DrawSeaweed(bool brightLayer);
    void DrawSeahorses();
    void DrawTurtle();
    void DrawJellyfish();
    void DrawFish();
    void DrawCrab();
    void DrawFood();
    void DrawBubbles();
    void DrawRipples();
    void DrawSparkles();

    void SpawnBubble(float x, float y, int size = -1, bool golden = false);

    float RandFloat(float a, float b);
    int RandInt(int a, int b);

    int cols_ = 0;
    int rows_ = 0;
    int bottomMarginRows_ = 3;
    int themeIndex_ = 0;
    float time_ = 0.0f;

    std::vector<Cell> grid_;
    std::vector<int> floorHeights_;
    std::vector<wchar_t> sandNoise_;

    std::vector<Fish> fishes_;
    SeaTurtle turtle_{};
    std::vector<Seahorse> seahorses_;
    std::vector<Jellyfish> jellies_;
    std::vector<Seaweed> seaweeds_;
    std::vector<Bubble> bubbles_;
    std::vector<FoodPellet> foods_;
    std::vector<Ripple> ripples_;
    std::vector<Sparkle> sparkles_;
    std::vector<Coral> corals_;
    std::vector<Rock> rocks_;
    std::vector<ShellDecor> shells_;
    TreasureChest chest_{};
    Crab crab_{};

    bool mouseActive_ = false;
    float mouseX_ = -100.0f;
    float mouseY_ = -100.0f;
    float mouseVx_ = 0.0f;
    float mouseVy_ = 0.0f;
    float mouseSpeed_ = 0.0f;

    std::mt19937 rng_;
};
