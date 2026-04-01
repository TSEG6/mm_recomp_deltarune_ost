
// Based on Ben's Remastered Soundtrack

#include "global.h"
#include "recomp/modding.h"
#include "recomputils.h"
#include "audio_api/sequence.h"
#include "audio_api/porcelain.h"
#include "recomp/recompconfig.h"
#include "z64save.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"
#include "z_dm_stk.h"
#include "assets/objects/object_stk2/object_stk2.h"
#include "assets/objects/object_stk3/object_stk3.h"
#include "z64player.h"

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_GetSeqPlayerSeqId(SequencePlayer* seqPlayer));

#define REMASTER_CHANNEL 0
#define OST_CHANNEL 1                   // Unused
#define REMASTER_VOLUME 1.33f
#define OST_VOLUME 0.0f                 // Unused
#define CROSSFADE_DURATION_TICKS 20     // Unused


// A lot of the fading between replacements and originals have been "left in" (just forcefully disabled)

static int activeChannel = REMASTER_CHANNEL;
static f32 remasterVolume;
static f32 ostVolume;
static int fadeTimer;
static f32 fadeInCurve[CROSSFADE_DURATION_TICKS];
static f32 fadeOutCurve[CROSSFADE_DURATION_TICKS];
static s32 lastScene = -1;
static s32 lastRoom = -1;
static const char* modPath = NULL;
static int SwordValue = 0;
static int Swoon = 0;
static bool pendingSwoon = false;
bool disableSwitchingOnCurrentTrack[SEQ_PLAYER_MAX];
int MusicRandoActive;

RECOMP_CALLBACK("*", recomp_on_init) void on_init() {

    MusicRandoActive = recomp_is_dependency_met("mm_music_randomizer") == DEPENDENCY_STATUS_FOUND;
}

typedef enum {

    /* 31 */ SK_ANIM_OCARINA_JUGGLE,

} SkullKidAnimation;

static int seqPlayers[] = {
    SEQ_PLAYER_BGM_MAIN,
    SEQ_PLAYER_FANFARE,
    SEQ_PLAYER_BGM_SUB,
};

typedef enum {
    STREAM_BGM = 0,
    STREAM_FANFARE = 1,
} ostStreamKind;

// This is the struct for the music replacements
typedef struct {
    s32 key;                
    char* files[6];         
    int numVariants;        // How many variants of music there is (I maybe have added 1 extra to each and now it's just the trend)
    int currentVariant;     // Variant selected
    ostStreamKind kind;
    AudioApiSequenceIO seqIO;
    u8 bankNo;
    bool replaced;
    int lastLoadedVariant;  // No more reloading every scene change (winning)
} ostSeqMapVar;

static ostSeqMapVar* GetSpecBySeqId(s32 seqId);


// A quick "guide" on how variants and the struct works

// {
//    NA_BGM_FILE_SELECT,               The game sequence that's going to get streamed in
// 
// { "NA_BGM_FILE_SELECT.ogg",          This is the basic variant (option 0) this will play by default without any changes
// 
//   "NA_BGM_FILE_SELECT_2.ogg",        Variant 1 (option 1)
// 
//   "NA_BGM_FILE_SELECT_3.ogg" },      Variant 2 (option 2)
// 
// 
//                                      To change the variants you need to set kSeqs[insert number here (in struct for this it would be 22)].currentVariant = (insert variant number here);
// 
//                                      You then need to reload and bind the variant by doing something like LoadAndBindStreamedSequenceVar(&kSeqs[22 (use your number)]);
// 
// 
// 3,                                   This is how many variants you have for this replacement (I keep setting it to one more than I need)
// 
// 0,                                   This is the currently select variant (usually changed by code unless done manually in the struct)
// 
// STREAM_BGM,                          This is the type of streamed audio used from enum ostStreamKind (fanfare or bgm)
// 
// AUDIOAPI_SEQ_IO_NONE,                This controls the special behavior the replacement has, such as dogs following you when you use the bremen mask
// 
// 0,                                   This is the sound bank number, used for compatibility
// 
// false,                               This checks whether the replacement was successful (I just realized I started these with "this" a whole bunch lol)
// 
// -1                                   This variable is used for optimization purposes so it doesn't need to reload it every scene change if it remains the same
// 
// },


// The Big Music Struct 
// (Add or remove variants here, or rename audio files if you don't like the names used)

static ostSeqMapVar kSeqs[] = {
    { NA_BGM_TERMINA_FIELD,              { "NA_BGM_TERMINA_FIELD.ogg",
                                           "NA_BGM_TERMINA_FIELD_SWORD.ogg", 
                                           "NA_BGM_TERMINA_FIELD_SWORD_SLOW.ogg"},               3, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CHASE,                      { "NA_BGM_CHASE.ogg" },                                 1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAJORAS_THEME,              { "NA_BGM_MAJORAS_THEME.ogg",
                                           "NA_BGM_MAJORAS_THEME_ROOF.ogg" },                    2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLOCK_TOWER,                { "NA_BGM_CLOCK_TOWER.ogg" },                           1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_STONE_TOWER_TEMPLE,         { "NA_BGM_STONE_TOWER_TEMPLE.ogg" },                    1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_INV_STONE_TOWER_TEMPLE,     { "NA_BGM_INV_STONE_TOWER_TEMPLE.ogg" },                1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FAILURE_0,                  { "NA_BGM_FAILURE_1.ogg",
                                           "KNIGHT_ROAR.ogg" },                                  2, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FAILURE_1,                  { "NA_BGM_FAILURE_1.ogg" },                             1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_HAPPY_MASK_SALESMAN,        { "NA_BGM_HAPPY_MASK_SALESMAN.ogg" },                   1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SONG_OF_HEALING,            { "NA_BGM_SONG_OF_HEALING.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SWAMP_REGION,               { "NA_BGM_SWAMP_REGION.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ALIEN_INVASION,             { "NA_BGM_ALIEN_INVASION.ogg",
                                           "NA_BGM_ALIEN_SCENE.ogg" },                           2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SWAMP_CRUISE,               { "NA_BGM_SWAMP_CRUISE.ogg",
                                           "NA_BGM_SWAMP_CRUISE_ALT.ogg" },                      2, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SHARPS_CURSE,               { "NA_BGM_SHARPS_CURSE.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GREAT_BAY_REGION,           { "NA_BGM_GREAT_BAY_REGION.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_IKANA_REGION,               { "NA_BGM_IKANA_REGION.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_DEKU_PALACE,                { "NA_BGM_DEKU_PALACE.ogg" },                           1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MOUNTAIN_REGION,            { "NA_BGM_MOUNTAIN_REGION.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_PIRATES_FORTRESS,           { "NA_BGM_PIRATES_FORTRESS.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLOCK_TOWN_DAY_1,           { "NA_BGM_CLOCK_TOWN_DAY_1.ogg",
                                           "NA_BGM_CLOCK_TOWN_DAY_4.ogg" },                      2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLOCK_TOWN_DAY_2,           { "NA_BGM_CLOCK_TOWN_DAY_2.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLOCK_TOWN_DAY_3,           { "NA_BGM_CLOCK_TOWN_DAY_3.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FILE_SELECT,                { "NA_BGM_FILE_SELECT.ogg",
                                           "NA_BGM_FILE_SELECT_2.ogg",
                                           "NA_BGM_FILE_SELECT_3.ogg" },                         3, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLEAR_EVENT,                { "NA_BGM_CLEAR_EVENT.ogg" },                           1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ENEMY,                      { "NA_BGM_ENEMY.ogg",
                                           "NA_BGM_ENEMY_2.ogg", 
                                           "NA_BGM_ENEMY_3.ogg",
                                           "NA_BGM_CHASE.ogg"},                               4, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_BOSS,                       { "NA_BGM_BOSS.ogg",
                                           "NA_BGM_BOSS_2.ogg",
                                           "NA_BGM_BOSS_3.ogg",
                                           "NA_BGM_BOSS_4.ogg" },                                4, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_WOODFALL_TEMPLE,            { "NA_BGM_WOODFALL_TEMPLE.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_OPENING,                    { "NA_BGM_OPENING.ogg" },                               1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_INSIDE_A_HOUSE,             { "NA_BGM_INSIDE_A_HOUSE.ogg" },                        1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GAME_OVER,                  { "NA_BGM_GAME_OVER.ogg",
                                           "NA_BGM_SWOON.ogg" },                                 2, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CLEAR_BOSS,                 { "NA_BGM_CLEAR_BOSS.ogg" },                            1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GET_ITEM,                   { "NA_BGM_GET_ITEM.ogg"},                               1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GET_HEART,                  { "NA_BGM_GET_HEART.ogg",
                                           "KNIGHT_WIND.ogg"},                                   2, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_TIMED_MINI_GAME,            { "NA_BGM_TIMED_MINI_GAME.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GORON_RACE,                 { "NA_BGM_GORON_RACE.ogg" },                            1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MUSIC_BOX_HOUSE,            { "NA_BGM_MUSIC_BOX_HOUSE.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ZELDAS_LULLABY,             { "NA_BGM_ZELDAS_LULLABY.ogg" },                        1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ROSA_SISTERS,               { "NA_BGM_ROSA_SISTERS.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_OPEN_CHEST,                 { "NA_BGM_OPEN_CHEST.ogg" },                            1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MARINE_RESEARCH_LAB,        { "NA_BGM_MARINE_RESEARCH_LAB.ogg",
                                           "NA_BGM_MARINE_RESEARCH_LAB_2.ogg" },                 1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GIANTS_THEME,               { "NA_BGM_GIANTS_THEME.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SONG_OF_STORMS,             { "NA_BGM_SONG_OF_STORMS.ogg" },                        1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ROMANI_RANCH,               { "NA_BGM_ROMANI_RANCH.ogg",
                                           "NA_BGM_ROMANI_RANCH_ALT.ogg" },                      2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GORON_VILLAGE,              { "NA_BGM_GORON_VILLAGE.ogg" },                         1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAYORS_OFFICE,              { "NA_BGM_MAYORS_OFFICE.ogg" },                         1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ZORA_HALL,                  { "NA_BGM_ZORA_HALL.ogg",
                                           "NA_BGM_ZORA_HALL_ALT.ogg" },                         2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GET_NEW_MASK,               { "NA_BGM_GET_NEW_MASK.ogg" },                          1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MINI_BOSS,                  { "NA_BGM_MINI_BOSS.ogg", 
                                           "NA_BGM_MINI_BOSS_2.ogg",
                                           "NA_BGM_ENEMY_3.ogg",
                                           "NA_BGM_MINI_BOSS_ROOF.ogg",
                                           "NA_BGM_MINI_BOSS_BK.ogg", 
                                           "NA_BGM_MBHCLOSET.ogg" },                             5, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GET_SMALL_ITEM,             { "NA_BGM_GET_SMALL_ITEM.ogg" },                        1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ASTRAL_OBSERVATORY,         { "NA_BGM_ASTRAL_OBSERVATORY.ogg" },                    1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CAVERN,                     { "NA_BGM_CAVERN.ogg",
                                           "NA_BGM_CAVERN_2.ogg",
                                           "NA_BGM_CAVERN_3.ogg"},                               2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MILK_BAR,                   { "NA_BGM_MILK_BAR.ogg" },                              1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ZELDA_APPEAR,               { "NA_BGM_ZELDA_APPEAR.ogg" },                          1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SARIAS_SONG,                { "NA_BGM_SARIAS_SONG.ogg" },                           1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GORON_GOAL,                 { "NA_BGM_GORON_GOAL.ogg" },                            1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_HORSE,                      { "NA_BGM_HORSE.ogg" },                                 1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_HORSE_GOAL,                 { "NA_BGM_HORSE_GOAL.ogg" },                            1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_INGO,                       { "NA_BGM_INGO.ogg" },                                  1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_KOTAKE_POTION_SHOP,         { "NA_BGM_KOTAKE_POTION_SHOP.ogg" },                    1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SHOP,                       { "NA_BGM_SHOP.ogg",
                                           "NA_BGM_SHOP_2.ogg",
                                           "NA_BGM_SHOP_3.ogg"},                                 3, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_OWL,                        { "NA_BGM_OWL.ogg" },                                   1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SHOOTING_GALLERY,           { "NA_BGM_SHOOTING_GALLERY.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SONATA_OF_AWAKENING,        { "NA_BGM_SONATA_OF_AWAKENING.ogg" },                   1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GORON_LULLABY,              { "NA_BGM_GORON_LULLABY.ogg" },                         1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_NEW_WAVE_BOSSA_NOVA,        { "NA_BGM_NEW_WAVE_BOSSA_NOVA.ogg" },                   1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_NEW_WAVE_SAXOPHONE,         { "NA_BGM_NEW_WAVE_BOSSA_NOVA.ogg" },                   1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_NEW_WAVE_VOCAL,             { "NA_BGM_NEW_WAVE_VOCAL.ogg" },                        1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_ELEGY_OF_EMPTINESS,         { "NA_BGM_ELEGY_OF_EMPTINESS.ogg" },                    1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_OATH_TO_ORDER,              { "NA_BGM_OATH_TO_ORDER.ogg" },                         1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SWORD_TRAINING_HALL,        { "NA_BGM_SWORD_TRAINING_HALL.ogg" },                   1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_LEARNED_NEW_SONG,           { "NA_BGM_LEARNED_NEW_SONG.ogg" },                      1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_BREMEN_MARCH,               { "NA_BGM_BREMEN_MARCH.ogg" },                          1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_BREMEN, 0, false, -1 },
    { NA_BGM_BALLAD_OF_THE_WIND_FISH,    { "NA_BGM_BALLAD_OF_THE_WIND_FISH.ogg" },               1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_WINDFISH, 0, false, -1 },
    { NA_BGM_SONG_OF_SOARING,            { "NA_BGM_SONG_OF_SOARING.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FINAL_HOURS,                { "NA_BGM_FINAL_HOURS.ogg",
                                           "NA_BGM_MAJORA.ogg" },                                2, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MIKAU_RIFF,                 { "NA_BGM_MIKAU_RIFF.ogg" },                            1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MIKAU_FINALE,               { "NA_BGM_MIKAU_FINALE.ogg" },                          1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FROG_SONG,                  { "NA_BGM_FROG_SONG.ogg" },                             1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_FROG, 0, false, -1 },
    { NA_BGM_PIANO_SESSION,              { "NA_BGM_PIANO_SESSION.ogg" },                         1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_INDIGO_GO_SESSION,          { "NA_BGM_INDIGO_GO_SESSION.ogg" },                     1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SNOWHEAD_TEMPLE,            { "NA_BGM_SNOWHEAD_TEMPLE.ogg" },                       1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GREAT_BAY_TEMPLE,           { "NA_BGM_GREAT_BAY_TEMPLE.ogg" },                      1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAJORAS_WRATH,              { "NA_BGM_MAJORAS_WRATH.ogg" },                         1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAJORAS_INCARNATION,        { "NA_BGM_MAJORAS_INCARNATION.ogg" },                   1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAJORAS_MASK,               { "NA_BGM_MAJORAS_MASK.ogg" },                          1, 0, STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_BASS_PLAY,                  { "NA_BGM_BASS_PLAY.ogg" },                             1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_DRUMS_PLAY,                 { "NA_BGM_DRUMS_PLAY.ogg" },                            1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_PIANO_PLAY,                 { "NA_BGM_PIANO_PLAY.ogg" },                            1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_IKANA_CASTLE,               { "NA_BGM_IKANA_CASTLE.ogg" },                          1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GATHERING_GIANTS,           { "NA_BGM_GATHERING_GIANTS.ogg" },                      1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_KAMARO_DANCE,               { "NA_BGM_KAMARO_DANCE.ogg" },                          1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_CREMIA_CARRIAGE,            { "NA_BGM_CREMIA_CARRIAGE.ogg" },                       1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_KEATON_QUIZ,                { "NA_BGM_KEATON_QUIZ.ogg" },                           1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_END_CREDITS,                { "NA_BGM_END_CREDITS.ogg" },                           1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_CREDITS_1, 0, false, -1 },
    { NA_BGM_TITLE_THEME,                { "NA_BGM_TITLE_THEME.ogg" },                           1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_DUNGEON_APPEAR,             { "NA_BGM_DUNGEON_APPEAR.ogg" },                        1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_WOODFALL_CLEAR,             { "NA_BGM_WOODFALL_CLEAR.ogg" },                        1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_SNOWHEAD_CLEAR,             { "NA_BGM_SNOWHEAD_CLEAR.ogg" },                        1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_INTO_THE_MOON,              { "NA_BGM_INTO_THE_MOON.ogg" },                         1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_GOODBYE_GIANT,              { "NA_BGM_GOODBYE_GIANT.ogg" },                         1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_TATL_AND_TAEL,              { "NA_BGM_TATL_AND_TAEL.ogg" },                         1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MOONS_DESTRUCTION,          { "NA_BGM_MOONS_DESTRUCTION.ogg" },                     1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_OCARINA_GUITAR_BASS_SESSION,{ "NA_BGM_OCARINA_GUITAR_BASS_SESSION.ogg" },           1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_END_CREDITS_SECOND_HALF,    { "NA_BGM_END_CREDITS_SECOND_HALF.ogg" },               1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_CREDITS_2, 0, false, -1 },
    { NB_BGM_MORNING,                    { "NB_BGM_MORNING.ogg" },                               1, 0, STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },

    { NA_BGM_CLOCK_TOWN_DAY_2_PTR,       {"NA_BGM_CLOCK_TOWN_DAY_2.ogg"},                        1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_FAIRY_FOUNTAIN,             {"NA_BGM_FAIRY_FOUNTAIN.ogg"},                          1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MILK_BAR_DUPLICATE,         {"NA_BGM_MILK_BAR.ogg" },                               1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },
    { NA_BGM_MAJORAS_LAIR,               {"NA_BGM_MAJORA.ogg" },                                 1, 0, STREAM_BGM, AUDIOAPI_SEQ_IO_NONE, 0, false, -1 },

};

// It loads and binds the streamed audio, that's all I really have to say

static void LoadAndBindStreamedSequenceVar(ostSeqMapVar* spec) {

    if (spec->currentVariant == spec->lastLoadedVariant) {
        return;
    }

    if (MusicRandoActive) {

        if (spec->lastLoadedVariant != -1) {
            return;
        }
    }

    char* fileToLoad = spec->files[spec->currentVariant];
    s32 seqId;

    switch (spec->kind) {
    case STREAM_BGM:
        seqId = AudioApi_CreateStreamedBgm(NULL, (char*)modPath, fileToLoad, spec->seqIO);
        spec->bankNo = AudioApi_GetSequenceFont(seqId, 0);
        break;
    case STREAM_FANFARE:
        seqId = AudioApi_CreateStreamedFanfare(NULL, (char*)modPath, fileToLoad, spec->seqIO);
        spec->bankNo = AudioApi_GetSequenceFont(seqId, 0);
        break;
    default:
        return;
    }

    if (seqId >= 0) {
        AudioApi_ReplaceSequence(spec->key, &gAudioCtx.sequenceTable->entries[seqId]);
        AudioApi_ReplaceSequenceFont(spec->key, 0, spec->bankNo);
        spec->replaced = true;
        spec->lastLoadedVariant = spec->currentVariant;
    }
}

// This was originally just for the boss music hence the name "UpdateBossMusicByScene" but now it just handles most of the variant switching and I don't feel like changing the name

static void UpdateBossMusicByScene(PlayState* play) {
    s32 scene = play->sceneId;
    s32 room = play->roomCtx.curRoom.num;
    Player* player = GET_PLAYER(play);

    if (!play) return; // for some reason this would return rarely false and crash the game???
    if (play->roomCtx.curRoom.segment == NULL) return;
    if (play->sceneId < 0) return;

    // This changes the theme of Romani Ranch depending on if Romani and the cows were captured
    if (CURRENT_DAY > 1) {

        if (!CHECK_WEEKEVENTREG(WEEKEVENTREG_DEFENDED_AGAINST_ALIENS)) {

            kSeqs[42].currentVariant = 1;
        }
        else {

            kSeqs[42].currentVariant = 0;
        }
    }
    else {

        kSeqs[42].currentVariant = 0;
    }
    LoadAndBindStreamedSequenceVar(&kSeqs[42]);

    // This controls the clock town theme for Day 4 & the shop theme day variants
    if (CURRENT_DAY < 4) {

        // We love day 0 crash preventions
        int day = CURRENT_DAY;
        if (day < 1) day = 1;

        kSeqs[59].currentVariant = day - 1;
        kSeqs[19].currentVariant = 0;

    }
    else {

        kSeqs[59].currentVariant = 0;
        kSeqs[19].currentVariant = 1;
    }
    LoadAndBindStreamedSequenceVar(&kSeqs[59]);
    LoadAndBindStreamedSequenceVar(&kSeqs[19]);

    // Controls the swamp cruise theme
    if (CURRENT_DAY == 3) {

        kSeqs[12].currentVariant = 1;

    }
    else {

        kSeqs[12].currentVariant = 0;

    }
    LoadAndBindStreamedSequenceVar(&kSeqs[12]);

    if (gSaveContext.save.isNight == false) {

        kSeqs[11].currentVariant = 1;

    }
    else {

        kSeqs[11].currentVariant = 0;

    }
    LoadAndBindStreamedSequenceVar(&kSeqs[11]);

    if (scene == SCENE_OKUJOU) {

        if (pendingSwoon) {

            kSeqs[29].currentVariant = 1;

        }
        LoadAndBindStreamedSequenceVar(&kSeqs[29]);

    }

    // Anti Spam(ton) (runs once per room and scene)
    // This is below the checks above because they need to be able to change without a scene change, if they were below in order for the audio to update you'd need to leave and return to the scene

    if (scene == lastScene && room == lastRoom) {
        return;
    }

    if (scene == SCENE_LAST_BS) {

        kSeqs[74].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[74]);

    }
    else {

        kSeqs[74].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[74]);
    }

    lastScene = scene;
    lastRoom = room;

    // Progression Battle Music
    // This works by checking the remains and if you have 2 or more it will use the second variant or if you're in sakon's hideout

    s32 remains = 0;
    if (CHECK_QUEST_ITEM(QUEST_REMAINS_ODOLWA))   remains++;
    if (CHECK_QUEST_ITEM(QUEST_REMAINS_GOHT))     remains++;
    if (CHECK_QUEST_ITEM(QUEST_REMAINS_GYORG))    remains++;
    if (CHECK_QUEST_ITEM(QUEST_REMAINS_TWINMOLD)) remains++;

    if (scene != SCENE_SECOM) {

        if (remains >= 2) {
            kSeqs[24].currentVariant = 1;
        }
        else {
            kSeqs[24].currentVariant = 0;
        }
    }
    else {

        kSeqs[24].currentVariant = 3;

    }
    LoadAndBindStreamedSequenceVar(&kSeqs[24]);

    // Termina Field Themes (based on your current sword)

    if (player->transformation == PLAYER_FORM_HUMAN) {

        if (CUR_FORM_EQUIP(EQUIP_SLOT_B) == ITEM_SWORD_RAZOR) {

            kSeqs[0].currentVariant = 1;
            SwordValue = 1;

        }
        else {

            if (CUR_FORM_EQUIP(EQUIP_SLOT_B) == ITEM_SWORD_GILDED) {

                kSeqs[0].currentVariant = 2;
                SwordValue = 2;

            }
            else {

                kSeqs[0].currentVariant = 0;
                SwordValue = 0;
            }
        }
    }
    else {

        kSeqs[0].currentVariant = SwordValue;

    }
    LoadAndBindStreamedSequenceVar(&kSeqs[0]);

    // Room & Scene Specific Changes (add or remove for scene specific changes) (that's a lot of cases)

    switch (scene) {
    case SCENE_MITURIN_BS:      // Odolwa
        kSeqs[25].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[25]);
        break;

    case SCENE_HAKUGIN_BS:      // Goht
        kSeqs[25].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[25]);
        break;

    case SCENE_SEA_BS:          // Gyorg
        kSeqs[25].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[25]);
        break;

    case SCENE_INISIE_BS:       // Twinmold
        kSeqs[25].currentVariant = 3;
        LoadAndBindStreamedSequenceVar(&kSeqs[25]);
        break;

    case SCENE_KAKUSIANA:       // Grotto
        if (room == 12) {
            kSeqs[50].currentVariant = 1;
        }
        else {
            kSeqs[50].currentVariant = 0;
        }
        LoadAndBindStreamedSequenceVar(&kSeqs[50]);
        break;

        // Not moved above scene check since it breaks when the day transition occurs for whatever reason
    case SCENE_33ZORACITY:       // Zora Hall
        if (CURRENT_DAY == 3) {
            kSeqs[45].currentVariant = 1;
        }
        else {
            kSeqs[45].currentVariant = 0;
        }
        LoadAndBindStreamedSequenceVar(&kSeqs[45]);
        break;

    case SCENE_SEA:       // GBT
        kSeqs[47].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        break;

    case SCENE_INISIE_N:       // STT
        kSeqs[47].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        break;

    case SCENE_INISIE_R:       // ISTT Gomess
        if (room == 11) {
            kSeqs[47].currentVariant = 2;
        }
        else {
            kSeqs[47].currentVariant = 1;
        }
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        break;

    case SCENE_LAST_LINK:       // Moon
        kSeqs[47].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        break;

    case SCENE_OKUJOU:       // CTR
        kSeqs[47].currentVariant = 4;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        kSeqs[6].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[6]);
        kSeqs[32].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[32]);

        if (remains == 4) { // Remains check for alternate tracks

            kSeqs[2].currentVariant = 1;
            LoadAndBindStreamedSequenceVar(&kSeqs[2]);
            kSeqs[47].currentVariant = 3;
            LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        }
        break;

    case SCENE_TOUGITES:       // Spirit House
        kSeqs[47].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        kSeqs[39].currentVariant = 1;
        LoadAndBindStreamedSequenceVar(&kSeqs[39]);
        break;

    case SCENE_RANDOM:       // Secret Shrine
        kSeqs[47].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        kSeqs[50].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[50]);
        break;

    case SCENE_REDEAD:       // BOTW (not the game)
        kSeqs[50].currentVariant = 2;
        LoadAndBindStreamedSequenceVar(&kSeqs[50]);
        break;

    case SCENE_MUSICHOUSE:   // Music Box House
        kSeqs[47].currentVariant = 5;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        break;

    default:
        kSeqs[47].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[47]);
        kSeqs[39].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[39]);
        kSeqs[2].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[2]);
        kSeqs[6].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[6]);
        kSeqs[32].currentVariant = 0;
        LoadAndBindStreamedSequenceVar(&kSeqs[32]);
        break;
    }
    // I wasn't kidding about all the cases
}

// Title Screen Progression
// This checks the remains once again, but this time it checks the save files and gets the one with the furthest progress
// The way it works is if you have 0 remains it plays variant 0, if you have 1 it plays 1, and if you have them all it plays 2 (you could do one for each or check something else)

static void UpdateFileSelectMusic(FileSelectState* fileSelect) {
    s32 fsVariant = 0;
    s32 slotVariants[2] = { 0, 0 };

    
    const QuestItem remainsEnum[] = {
        QUEST_REMAINS_ODOLWA,
        QUEST_REMAINS_GOHT,
        QUEST_REMAINS_GYORG,
        QUEST_REMAINS_TWINMOLD
    };

    for (s32 slot = 0; slot < 2; slot++) {
        u16 questItems = fileSelect->questItems[slot];
        s32 remains = 0;

        for (s32 i = 0; i < 4; i++) {
            if (questItems & (1 << remainsEnum[i])) {
                remains++;
            }
        }

        if (remains == 4) slotVariants[slot] = 2;
        else if (remains > 0) slotVariants[slot] = 1;
        else slotVariants[slot] = 0;
    }

    fsVariant = slotVariants[0] > slotVariants[1] ? slotVariants[0] : slotVariants[1];

    if (kSeqs[22].currentVariant != fsVariant) {
        kSeqs[22].currentVariant = fsVariant;
        LoadAndBindStreamedSequenceVar(&kSeqs[22]);
    }
}

// Update Game

RECOMP_HOOK("Play_Update") void OnPlayUpdate(PlayState* play) {

    UpdateBossMusicByScene(play);

    // Swoon

    if (pendingSwoon && play->msgCtx.msgMode == MSGMODE_NONE) {

        gSaveContext.save.saveInfo.playerData.health = 0;
        Play_EnableMotionBlur(230);
        gPlayVisMonoColor.r = 255;
        gPlayVisMonoColor.g = 255;
        gPlayVisMonoColor.b = 255;
        gPlayVisMonoColor.a = 255;

    }
}

// Update File Select

RECOMP_HOOK("FileSelect_UpdateAndDrawSkybox") void OnFileUpdate( FileSelectState* fileSelect) {

    UpdateFileSelectMusic(fileSelect);
}


static ostSeqMapVar* GetSpecBySeqId(s32 seqId) {
    int i;
    for (i = 0; i < ARRAY_COUNT(kSeqs); ++i) {
        if (kSeqs[i].replaced && kSeqs[i].key == seqId) {
            return &kSeqs[i];
        }
    }
    return NULL;
}

RECOMP_CALLBACK("magemods_audio_api", AudioApi_Init) void onAudioApiInit() {

    if (modPath == NULL) {
        modPath = (const char*)recomp_get_mod_file_path();

        if (modPath == NULL) {
            modPath = "";
        }
    }

    int i;
    for (i = 0; i < ARRAY_COUNT(kSeqs); ++i) {
        LoadAndBindStreamedSequenceVar(&kSeqs[i]);
    }

    for (i = 0; i < CROSSFADE_DURATION_TICKS; i++) {
        fadeInCurve[i] = Math_SinF((f32)i / CROSSFADE_DURATION_TICKS * M_PI * 0.5f);
        fadeOutCurve[i] = Math_CosF((f32)i / CROSSFADE_DURATION_TICKS * M_PI * 0.5f);
    }
}


RECOMP_HOOK("AudioScript_ProcessSequences") void onProcessSequences() {
    f32 fadeIn, fadeOut;

    if (fadeTimer > 0) {
        fadeIn = fadeInCurve[CROSSFADE_DURATION_TICKS - fadeTimer];
        fadeOut = fadeOutCurve[CROSSFADE_DURATION_TICKS - fadeTimer];
        fadeTimer--;
    }
    else {
        fadeIn = 1.0f;
        fadeOut = 0.0f;
    }

    if (activeChannel == REMASTER_CHANNEL) {
        remasterVolume = fadeIn * REMASTER_VOLUME;
        ostVolume = fadeOut * OST_VOLUME;
    }
    else {
        activeChannel = REMASTER_CHANNEL;
    }
}


RECOMP_HOOK("AudioScript_SequencePlayerProcessSound") void onSequencePlayerProcessSound(SequencePlayer* seqPlayer) {
    s32 seqId;
    ostSeqMapVar* spec;
    SequenceChannel* channel;
    f32 volume;
    int i;

    seqId = AudioApi_GetSeqPlayerSeqId(seqPlayer);
    spec = GetSpecBySeqId(seqId);

    if (disableSwitchingOnCurrentTrack[seqPlayer->playerIndex]) { return; }

    if (spec) {
        for (i = 0; i < ARRAY_COUNT(seqPlayer->channels); i++) {
            channel = seqPlayer->channels[i];
            if (!channel) continue;

            volume = remasterVolume;

            if (channel->volume != volume) {
                channel->volume = volume;
                channel->changes.s.volume = true;
            }
        }
    }
}

// This controls what variant plays

void SetTrackVariant(s32 seqKey, int variant) {
    int i;
    for (i = 0; i < ARRAY_COUNT(kSeqs); i++) {
        if (kSeqs[i].key == seqKey && variant < kSeqs[i].numVariants) {
            kSeqs[i].currentVariant = variant;
            LoadAndBindStreamedSequenceVar(&kSeqs[i]);
            break;
        }
    }
}

// The two patches below are related to the clock tower roof cutscenes for replacing Skull Kid's scream sound 
// (I found it easier to patch but may consider using hooks again in the future for more compat)

RECOMP_PATCH void DmStk_PlaySfxForClockTowerIntroCutsceneVersion1(DmStk* this, PlayState* play) {
    static bool sMoonCallPlayed = false;
    static s32 sMoonCallTimer = 0;
    double shouldWind = recomp_get_config_double("winding");

    switch (play->csCtx.curFrame) {
    case 140:
        Audio_PlaySfx_AtPosWithVolumeTransition(&this->actor.projectedPos, NA_SE_EN_STALKIDS_FLOAT, 80);
        break;

    case 258:
        Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_TURN);
        break;

    case 524:
        Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_TURN);
        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL04_ANGER);
        break;

    case 534:
        Actor_PlaySfx(&this->actor, NA_SE_EN_PO_ROLL);
        break;

    case 678:
        if (shouldWind) {
            Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_STRETCH);
        }
        else {

            if (MusicRandoActive) {

                Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_STRETCH);

            }
            else {

                Audio_PlayFanfare(NA_BGM_GET_HEART);

            }

        }
        break;

    default:
        break;
    }

    if ((this->animIndex == SK_ANIM_OCARINA_JUGGLE) && (play->csCtx.curFrame < 700)) {
        if (Animation_OnFrame(&this->skelAnime, 5.0f) || Animation_OnFrame(&this->skelAnime, 25.0f)) {
            Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_OTEDAMA1);
        }
        else if (Animation_OnFrame(&this->skelAnime, 17.0f) || Animation_OnFrame(&this->skelAnime, 40.0f)) {
            Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_OTEDAMA2);
        }
    }

    if (shouldWind) {
        if (MusicRandoActive) {
            if (play->csCtx.curFrame >= 700) {
                if (sMoonCallTimer < 128) {
                    if ((sMoonCallTimer & 0x1F) == 0) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON);
                    }
                    else if ((sMoonCallTimer & 0x1F) == 16) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON2);
                    }
                    sMoonCallTimer++;
                }
            }
            else {
                sMoonCallTimer = 0;
            }
        }
        else {
            if (play->csCtx.curFrame >= 700) {
                if (!sMoonCallPlayed) {
                    Audio_PlayFanfare(NA_BGM_FAILURE_0);
                    sMoonCallPlayed = true;
                }
            }
            else {
                sMoonCallPlayed = false;
            }
        }
    }
    else {

        if (play->csCtx.curFrame >= 700) {
            if (sMoonCallTimer < 128) {
                if ((sMoonCallTimer & 0x1F) == 0) {
                    Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON);
                }
                else if ((sMoonCallTimer & 0x1F) == 16) {
                    Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON2);
                }
                sMoonCallTimer++;
            }
        }
        else {
            sMoonCallTimer = 0;
        }
    }
}


RECOMP_PATCH void DmStk_PlaySfxForClockTowerIntroCutsceneVersion2(DmStk* this, PlayState* play) {
    static bool sMoonCallPlayed = false;
    static s32 sMoonCallTimer = 0;
    double shouldWind = recomp_get_config_double("winding");

    switch (play->csCtx.curFrame) {
    case 40:
        Audio_PlaySfx_AtPosWithVolumeTransition(&this->actor.projectedPos, NA_SE_EN_STALKIDS_FLOAT, 80);
        break;

    case 234:
        Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_TURN);
        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL04_ANGER);
        break;

    case 244:
        Actor_PlaySfx(&this->actor, NA_SE_EN_PO_ROLL);
        break;

    case 388:
        if (shouldWind) {
            Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_STRETCH);
        }
        else {

            if (MusicRandoActive) {

                Actor_PlaySfx(&this->actor, NA_SE_EN_STALKIDS_STRETCH);

            }
            else {

                Audio_PlayFanfare(NA_BGM_GET_HEART);

            }
        }
        break;

    default:
        break;
    }

    if (shouldWind) {
        if (MusicRandoActive) {

            if (play->csCtx.curFrame >= 408) {
                if (sMoonCallTimer < 128) {
                    if ((sMoonCallTimer & 0x1F) == 0) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON);
                    }
                    else if ((sMoonCallTimer & 0x1F) == 16) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON2);
                    }
                    sMoonCallTimer++;
                }
            }
            else {
                sMoonCallTimer = 0;
            }
        }
        else {
            if (play->csCtx.curFrame >= 408) {
                if (!sMoonCallPlayed) {
                    Audio_PlayFanfare(NA_BGM_FAILURE_0);
                    sMoonCallPlayed = true;
                }
            }
            else {
                sMoonCallPlayed = false;
            }
        }
    }
    else {

        if (MusicRandoActive) {

            if (play->csCtx.curFrame >= 408) {
                if (sMoonCallTimer < 128) {
                    if ((sMoonCallTimer & 0x1F) == 0) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON);
                    }
                    else if ((sMoonCallTimer & 0x1F) == 16) {
                        Actor_PlaySfx(&this->actor, NA_SE_EN_STAL20_CALL_MOON2);
                    }
                    sMoonCallTimer++;
                }
            }
            else {
                sMoonCallTimer = 0;
            }
        }

    }
}

// Swoon counter for when you attack skull kid

RECOMP_HOOK_RETURN("DmStk_ClockTower_DeflectHit")
void killlink(PlayState* play) {

    if (MusicRandoActive) {

        return;

    }

    Swoon++;

    if (Swoon >= 3) {

    pendingSwoon = true;

    }
}

// Swoon removal

RECOMP_HOOK("func_80169ECC")
void removeswooneffects(PlayState* play) {

    s32 scene = play->sceneId;

    if (scene == SCENE_OKUJOU) {

        pendingSwoon = false;
        kSeqs[29].currentVariant = 0;
        Swoon = 0;
        Play_EnableMotionBlur(0);
        gPlayVisMonoColor.r = 0;
        gPlayVisMonoColor.g = 0;
        gPlayVisMonoColor.b = 0;
        gPlayVisMonoColor.a = 0;

    }
}

// SOT Swoon Removal

RECOMP_HOOK("DayTelop_Main")
void resetredeffectplease() {

    kSeqs[29].currentVariant = 0;
    Swoon = 0;
    pendingSwoon = false;
}

// Fairy Remove Swoon (this also just plays regardless of having a fairy so it's not really working as intended but I'm keeping it because it does the job)

RECOMP_HOOK("func_80840770")
void removeswooneffectsbutwithafairy(PlayState* play) {

    s32 scene = play->sceneId;

    if (scene == SCENE_OKUJOU) {

        pendingSwoon = false;
        kSeqs[29].currentVariant = 0;
        Swoon = 0;
        Play_EnableMotionBlur(0);
        gPlayVisMonoColor.r = 0;
        gPlayVisMonoColor.g = 0;
        gPlayVisMonoColor.b = 0;
        gPlayVisMonoColor.a = 0;

    }
}