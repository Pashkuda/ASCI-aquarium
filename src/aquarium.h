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
    int desiredFacing;   // smooth turning target
    int depthLayer;      // 0 = far, 1 = mid, 2 = near
    int colorIndex;
    int variant;
    float phase;
    float burstTimer;
    float burstAmount;
    float pauseTimer;
    float scaredTimer;
    float hungryCooldown;
    float swimTargetY;
    float homeX, homeY;
    float turnTimer;
    float turnCooldown;
};

struct SeaTurtle {
    float x, y;
    float vx, vy;
    int facing;
    float phase;
    float targetY;
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
    float trailOffset;
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

struct Seagrass {
    int x;
    int height;
    float phase;
    bool bright;
};

struct MarineSpeck {
    float x, y;
    float vx, vy;
    float phase;
    wchar_t ch;
};

struct WaterSwirl {
    float x, y;
    float vx, vy;
    float radius;
    float angle;
    float spinSpeed;     // +CW or -CCW
    float age;
    float maxLife;
    float strength;
};

struct Shark {
    bool active = false;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float targetY = 0.0f;
    int facing = 1;
    float phase = 0.0f;
    float nextSpawnTimer = 0.0f; // 1800..3600 seconds (30-60 min)
    float wakeTimer = 0.0f;
};

enum class SubmarineState {
    Inactive,
    CruisingIn,
    Surfacing,
    PeriscopeUp,
    LookingAround,
    PeriscopeDown,
    Diving,
    CruisingOut
};

struct Submarine {
    SubmarineState state = SubmarineState::Inactive;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float cruiseY = 14.0f;
    float stopX = 60.0f;
    int facing = 1;           // +1 right, -1 left
    float propPhase = 0.0f;
    float periscopeExt = 0.0f; // 0.0 (retracted) .. 1.0 (fully raised)
    float stateTimer = 0.0f;
    int lookDir = 1;          // -1 left, 0 center, +1 right
    float nextSpawnTimer = 0.0f; // 1200..2700 seconds (20-45 min)
};

struct SurfaceBoat {
    bool active = false;
    float x = 0.0f;
    float vx = 0.0f;
    float baseVx = 0.0f;
    float yOffset = 0.0f;     // vertical lift when climbing a big wave (negative = up)
    float vyOffset = 0.0f;
    float rockAmp = 0.0f;     // post-wave damped rocking amplitude
    float rockPhase = 0.0f;
    int tilt = 0;             // -1 nose down, 0 level, +1 nose up
    int facing = 1;           // alternates +1 -> -1 -> +1 ...
    int nextFacing = 1;
    int style = 0;            // 0 = sailboat, 1 = trawler, 2 = schooner
    float animPhase = 0.0f;
    float nextSpawnTimer = 0.0f;
};

struct BigWave {
    bool active = false;
    float x = 0.0f;
    float vx = 0.0f;
    int facing = -1;          // -1 rolls right->left (breaks left), +1 rolls left->right (breaks right)
    float phase = 0.0f;
    float nextSpawnTimer = 0.0f; // 180..360 seconds (3-6 min)
};

struct Bubble {
    float x, y;
    float vy;
    float phase;
    float growTimer;
    int size;
    bool popping;
    float popTimer;
    bool golden;
};

struct FoodPellet {
    float x, y;
    float vx, vy;
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
    int style;       // 0 = staghorn, 1 = sea fan, 2 = animated anemone, 3 = tube sponge
    int colorSlot;
    float phase;
};

struct Rock {
    int x;
    int width;
    int height;
    bool mossy;
};

struct ShellDecor {
    int x;
    int dy;
    wchar_t ch;
    int kind;        // 0 = shell, 1 = starfish, 2 = pebble
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
    void SummonShark();
    void SummonSubmarine();
    void SummonBigWave();

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
    void PutMaskedSprite(int x, int y, const wchar_t* const* lines, const char* const* masks, int count,
                         COLORREF bodyCol, COLORREF finCol, COLORREF accentCol, COLORREF eyeCol);
    int FloorYAt(int x) const;
    float FloorHeightFAt(float x) const;
    float SurfaceWaveAt(float x) const;
    float CurrentXAt(float x, float y) const;
    float LightIntensityAt(float x, float y) const;
    COLORREF BgColorAtRow(int y) const;
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
    void UpdateMarineSnow(float dt);
    void UpdateSwirls(float dt);
    void UpdateShark(float dt);
    void UpdateSubmarine(float dt);
    void UpdateBoat(float dt);
    void UpdateBigWave(float dt);

    void DrawLightRays();
    void DrawMarineSnow();
    void DrawSwirls();
    void DrawBoat();
    void DrawBigWave();
    void DrawSurface();
    void DrawDistantSeabed();
    void DrawSeabed();
    void DrawSeagrass();
    void DrawTreasureChest();
    void DrawSeaweed(bool brightLayer);
    void DrawSeahorses();
    void DrawTurtle();
    void DrawShark();
    void DrawSubmarine(bool surfaceOverlay);
    void DrawJellyfish();
    void DrawFishLayer(int layer);
    void DrawSingleFish(const Fish& f);
    void DrawCrab();
    void DrawFood();
    void DrawBubbles();
    void DrawRipples();
    void DrawSparkles();

    void SpawnBubble(float x, float y, int size = -1, bool golden = false);
    void SpawnSwirl(float x, float y, float radius = 0.0f, float strength = 1.0f, float vx = 0.0f);
    void SampleSwirlFlow(float x, float y, float& outVx, float& outVy) const;

    float RandFloat(float a, float b);
    int RandInt(int a, int b);

    int cols_ = 0;
    int rows_ = 0;
    int bottomMarginRows_ = 3;
    int themeIndex_ = 0;
    float time_ = 0.0f;

    std::vector<Cell> grid_;
    std::vector<int> floorHeights_;
    std::vector<float> floorHeightF_;
    std::vector<float> bgFloorF_;
    std::vector<wchar_t> sandNoise_;

    std::vector<Fish> fishes_;
    SeaTurtle turtle_{};
    std::vector<Seahorse> seahorses_;
    std::vector<Jellyfish> jellies_;
    std::vector<Seaweed> seaweeds_;
    std::vector<Seagrass> seagrasses_;
    std::vector<MarineSpeck> specks_;
    std::vector<WaterSwirl> swirls_;
    Shark shark_{};
    Submarine sub_{};
    SurfaceBoat boat_{};
    BigWave bigWave_{};
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
    float surfaceDisturbX_ = -100.0f;
    float surfaceDisturbAmp_ = 0.0f;

    std::mt19937 rng_;
};
