#include "global.h"
#include "recomp/modding.h"
#include "recomp/recomputils.h"
#include "audio_api/sequence.h"
#include "audio_api/porcelain.h"
#include "recomp/recompconfig.h"

RECOMP_IMPORT("magemods_audio_api", s32 AudioApi_GetSeqPlayerSeqId(SequencePlayer* seqPlayer));
RECOMP_IMPORT("ProxyMM_Notifications", void Notifications_Emit(const char* prefix, const char* msg, const char* suffix));

#define REMASTER_CHANNEL 0
#define OST_CHANNEL 1
#define OST_VOLUME 1.0f               // between 0.0f - 2.0f
#define CROSSFADE_DURATION_TICKS 180  // 180 ticks = 1 second

// -3 dB = 0.707, 0 dB = 1.0, +3 dB = 1.413
static const f32 kRemasterVolumeTable[] = { 0.707f, 1.0f, 1.413f };

static int activeChannel = -1;
static f32 remasterVolumeMax = 1.0f;
static f32 remasterVolume;
static f32 ostVolume;
static int fadeTimer;
static f32 fadeInCurve[CROSSFADE_DURATION_TICKS];
static f32 fadeOutCurve[CROSSFADE_DURATION_TICKS];

static int seqPlayers[] = {
    SEQ_PLAYER_BGM_MAIN,
    SEQ_PLAYER_FANFARE,
    SEQ_PLAYER_BGM_SUB,
};

typedef enum {
    STREAM_BGM = 0,
    STREAM_FANFARE = 1
} ostStreamKind;

typedef struct {
    s32 key;            // NA_BGM_* enum to replace
    char file[64];      // writable filename buffer
    ostStreamKind kind; // STREAM_BGM or STREAM_FANFARE
    AudioApiSequenceIO seqIO; // sequence IO type (e.g. AUDIOAPI_SEQ_IO_BREMEN)
    bool replaced;      // was the replacement successful
} ostSeqMap;


// Keep only what you want replaced. Examples include your previous picks plus
// pointer variants that map to their target’s filename.
static ostSeqMap kSeqs[] = {
    { NA_BGM_TERMINA_FIELD,            "NA_BGM_TERMINA_FIELD.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CHASE,                    "NA_BGM_CHASE.ogg",                    STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAJORAS_THEME,            "NA_BGM_MAJORAS_THEME.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLOCK_TOWER,              "NA_BGM_CLOCK_TOWER.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_STONE_TOWER_TEMPLE,       "NA_BGM_STONE_TOWER_TEMPLE.ogg",       STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_INV_STONE_TOWER_TEMPLE,   "NA_BGM_INV_STONE_TOWER_TEMPLE.ogg",   STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FAILURE_0,                "NA_BGM_FAILURE_0.ogg",                STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FAILURE_1,                "NA_BGM_FAILURE_1.ogg",                STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_HAPPY_MASK_SALESMAN,      "NA_BGM_HAPPY_MASK_SALESMAN.ogg",      STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SONG_OF_HEALING,          "NA_BGM_SONG_OF_HEALING.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SWAMP_REGION,             "NA_BGM_SWAMP_REGION.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ALIEN_INVASION,           "NA_BGM_ALIEN_INVASION.ogg",           STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SWAMP_CRUISE,             "NA_BGM_SWAMP_CRUISE.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SHARPS_CURSE,             "NA_BGM_SHARPS_CURSE.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GREAT_BAY_REGION,         "NA_BGM_GREAT_BAY_REGION.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_IKANA_REGION,             "NA_BGM_IKANA_REGION.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_DEKU_PALACE,              "NA_BGM_DEKU_PALACE.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MOUNTAIN_REGION,          "NA_BGM_MOUNTAIN_REGION.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_PIRATES_FORTRESS,         "NA_BGM_PIRATES_FORTRESS.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLOCK_TOWN_DAY_1,         "NA_BGM_CLOCK_TOWN_DAY_1.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLOCK_TOWN_DAY_2,         "NA_BGM_CLOCK_TOWN_DAY_2.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLOCK_TOWN_DAY_3,         "NA_BGM_CLOCK_TOWN_DAY_3.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FILE_SELECT,              "NA_BGM_FILE_SELECT.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLEAR_EVENT,              "NA_BGM_CLEAR_EVENT.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ENEMY,                    "NA_BGM_ENEMY.ogg",                    STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_BOSS,                     "NA_BGM_BOSS.ogg",                     STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_WOODFALL_TEMPLE,          "NA_BGM_WOODFALL_TEMPLE.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_OPENING,                  "NA_BGM_OPENING.ogg",                  STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_INSIDE_A_HOUSE,           "NA_BGM_INSIDE_A_HOUSE.ogg",           STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GAME_OVER,                "NA_BGM_GAME_OVER.ogg",                STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CLEAR_BOSS,               "NA_BGM_CLEAR_BOSS.ogg",               STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GET_ITEM,                 "NA_BGM_GET_ITEM.ogg",                 STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GET_HEART,                "NA_BGM_GET_HEART.ogg",                STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_TIMED_MINI_GAME,          "NA_BGM_TIMED_MINI_GAME.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GORON_RACE,               "NA_BGM_GORON_RACE.ogg",               STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MUSIC_BOX_HOUSE,          "NA_BGM_MUSIC_BOX_HOUSE.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ZELDAS_LULLABY,           "NA_BGM_ZELDAS_LULLABY.ogg",           STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ROSA_SISTERS,             "NA_BGM_ROSA_SISTERS.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_OPEN_CHEST,               "NA_BGM_OPEN_CHEST.ogg",               STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MARINE_RESEARCH_LAB,      "NA_BGM_MARINE_RESEARCH_LAB.ogg",      STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GIANTS_THEME,             "NA_BGM_GIANTS_THEME.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SONG_OF_STORMS,           "NA_BGM_SONG_OF_STORMS.ogg",           STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ROMANI_RANCH,             "NA_BGM_ROMANI_RANCH.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GORON_VILLAGE,            "NA_BGM_GORON_VILLAGE.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAYORS_OFFICE,            "NA_BGM_MAYORS_OFFICE.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ZORA_HALL,                "NA_BGM_ZORA_HALL.ogg",                STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GET_NEW_MASK,             "NA_BGM_GET_NEW_MASK.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MINI_BOSS,                "NA_BGM_MINI_BOSS.ogg",                STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GET_SMALL_ITEM,           "NA_BGM_GET_SMALL_ITEM.ogg",           STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ASTRAL_OBSERVATORY,       "NA_BGM_ASTRAL_OBSERVATORY.ogg",       STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CAVERN,                   "NA_BGM_CAVERN.ogg",                   STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MILK_BAR,                 "NA_BGM_MILK_BAR.ogg",                 STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ZELDA_APPEAR,             "NA_BGM_ZELDA_APPEAR.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SARIAS_SONG,              "NA_BGM_SARIAS_SONG.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GORON_GOAL,               "NA_BGM_GORON_GOAL.ogg",               STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_HORSE,                    "NA_BGM_HORSE.ogg",                    STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_HORSE_GOAL,               "NA_BGM_HORSE_GOAL.ogg",               STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_INGO,                     "NA_BGM_INGO.ogg",                     STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_KOTAKE_POTION_SHOP,       "NA_BGM_KOTAKE_POTION_SHOP.ogg",       STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SHOP,                     "NA_BGM_SHOP.ogg",                     STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_OWL,                      "NA_BGM_OWL.ogg",                      STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SHOOTING_GALLERY,         "NA_BGM_SHOOTING_GALLERY.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SONATA_OF_AWAKENING,      "NA_BGM_SONATA_OF_AWAKENING.ogg",      STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GORON_LULLABY,            "NA_BGM_GORON_LULLABY.ogg",            STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_NEW_WAVE_BOSSA_NOVA,      "NA_BGM_NEW_WAVE_BOSSA_NOVA.ogg",      STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_NEW_WAVE_SAXOPHONE,       "NA_BGM_NEW_WAVE_BOSSA_NOVA.ogg",      STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_NEW_WAVE_VOCAL,           "NA_BGM_NEW_WAVE_VOCAL.ogg",           STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_ELEGY_OF_EMPTINESS,       "NA_BGM_ELEGY_OF_EMPTINESS.ogg",       STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_OATH_TO_ORDER,            "NA_BGM_OATH_TO_ORDER.ogg",            STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SWORD_TRAINING_HALL,      "NA_BGM_SWORD_TRAINING_HALL.ogg",      STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_LEARNED_NEW_SONG,         "NA_BGM_LEARNED_NEW_SONG.ogg",         STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_BREMEN_MARCH,             "NA_BGM_BREMEN_MARCH.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_BREMEN, false },
    { NA_BGM_BALLAD_OF_THE_WIND_FISH,  "NA_BGM_BALLAD_OF_THE_WIND_FISH.ogg",  STREAM_FANFARE, AUDIOAPI_SEQ_IO_WINDFISH, false },
    { NA_BGM_SONG_OF_SOARING,          "NA_BGM_SONG_OF_SOARING.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FINAL_HOURS,              "NA_BGM_FINAL_HOURS.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MIKAU_RIFF,               "NA_BGM_MIKAU_RIFF.ogg",               STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MIKAU_FINALE,             "NA_BGM_MIKAU_FINALE.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FROG_SONG,                "NA_BGM_FROG_SONG.ogg",                STREAM_BGM,     AUDIOAPI_SEQ_IO_FROG, false },
    { NA_BGM_PIANO_SESSION,            "NA_BGM_PIANO_SESSION.ogg",            STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_INDIGO_GO_SESSION,        "NA_BGM_INDIGO_GO_SESSION.ogg",        STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SNOWHEAD_TEMPLE,          "NA_BGM_SNOWHEAD_TEMPLE.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GREAT_BAY_TEMPLE,         "NA_BGM_GREAT_BAY_TEMPLE.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAJORAS_WRATH,            "NA_BGM_MAJORAS_WRATH.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAJORAS_INCARNATION,      "NA_BGM_MAJORAS_INCARNATION.ogg",      STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAJORAS_MASK,             "NA_BGM_MAJORAS_MASK.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_BASS_PLAY,                "NA_BGM_BASS_PLAY.ogg",                STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_DRUMS_PLAY,               "NA_BGM_DRUMS_PLAY.ogg",               STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_PIANO_PLAY,               "NA_BGM_PIANO_PLAY.ogg",               STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_IKANA_CASTLE,             "NA_BGM_IKANA_CASTLE.ogg",             STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GATHERING_GIANTS,         "NA_BGM_GATHERING_GIANTS.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_KAMARO_DANCE,             "NA_BGM_KAMARO_DANCE.ogg",             STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_CREMIA_CARRIAGE,          "NA_BGM_CREMIA_CARRIAGE.ogg",          STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_KEATON_QUIZ,              "NA_BGM_KEATON_QUIZ.ogg",              STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_END_CREDITS,              "NA_BGM_END_CREDITS.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_CREDITS_1, false },
    { NA_BGM_TITLE_THEME,              "NA_BGM_TITLE_THEME.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_DUNGEON_APPEAR,           "NA_BGM_DUNGEON_APPEAR.ogg",           STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_WOODFALL_CLEAR,           "NA_BGM_WOODFALL_CLEAR.ogg",           STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_SNOWHEAD_CLEAR,           "NA_BGM_SNOWHEAD_CLEAR.ogg",           STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_INTO_THE_MOON,            "NA_BGM_INTO_THE_MOON.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_GOODBYE_GIANT,            "NA_BGM_GOODBYE_GIANT.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_TATL_AND_TAEL,            "NA_BGM_TATL_AND_TAEL.ogg",            STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MOONS_DESTRUCTION,        "NA_BGM_MOONS_DESTRUCTION.ogg",        STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_OCARINA_GUITAR_BASS_SESSION,"NA_BGM_OCARINA_GUITAR_BASS_SESSION.ogg",STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false }, // Not Ocarina
    { NA_BGM_END_CREDITS_SECOND_HALF,  "NA_BGM_END_CREDITS_SECOND_HALF.ogg",  STREAM_BGM,     AUDIOAPI_SEQ_IO_CREDITS_2, false },
    { NB_BGM_MORNING, "NB_BGM_MORNING.ogg", STREAM_FANFARE, AUDIOAPI_SEQ_IO_NONE, false },

    // // --- POINTER VARIANTS ---
    { NA_BGM_CLOCK_TOWN_DAY_2_PTR,     "NA_BGM_CLOCK_TOWN_DAY_2.ogg",         STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_FAIRY_FOUNTAIN,           "NA_BGM_FAIRY_FOUNTAIN.ogg",           STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MILK_BAR_DUPLICATE,       "NA_BGM_MILK_BAR.ogg",                 STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },
    { NA_BGM_MAJORAS_LAIR,             "NA_BGM_FINAL_HOURS.ogg",              STREAM_BGM,     AUDIOAPI_SEQ_IO_NONE, false },

    // // --- Ocarina Songs ---
    // { NA_BGM_OCARINA_LULLABY_INTRO_PTR,"NA_BGM_OCARINA_LULLABY_INTRO.ogg",    STREAM_FANFARE, false }, // POINTER!!!
    // { NA_BGM_OCARINA_LULLABY_INTRO,    "NA_BGM_OCARINA_LULLABY_INTRO.ogg",    STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_EPONA,            "NA_BGM_OCARINA_EPONA.ogg",            STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_SUNS,             "NA_BGM_OCARINA_SUNS.ogg",             STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_TIME,             "NA_BGM_OCARINA_TIME.ogg",             STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_STORM,            "NA_BGM_OCARINA_STORM.ogg",            STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_SONATA,           "NA_BGM_OCARINA_SONATA.ogg",           STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_LULLABY,          "NA_BGM_OCARINA_LULLABY.ogg",          STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_NEW_WAVE,         "NA_BGM_OCARINA_NEW_WAVE.ogg",         STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_ELEGY,            "NA_BGM_OCARINA_ELEGY.ogg",            STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_OATH,             "NA_BGM_OCARINA_OATH.ogg",             STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_SOARING,          "NA_BGM_OCARINA_SOARING.ogg",          STREAM_FANFARE, false },
    // { NA_BGM_OCARINA_HEALING,          "NA_BGM_OCARINA_HEALING.ogg",          STREAM_FANFARE, false },
    // { NA_BGM_INVERTED_SONG_OF_TIME,    "NA_BGM_INVERTED_SONG_OF_TIME.ogg",    STREAM_FANFARE, false },
    // { NA_BGM_SONG_OF_DOUBLE_TIME,      "NA_BGM_SONG_OF_DOUBLE_TIME.ogg",      STREAM_FANFARE, false },

    // // --- ??? ---
    // { NA_BGM_OPENING_LOOP,             "NA_BGM_OPENING_LOOP.ogg",             STREAM_BGM,     false },
    // { NA_BGM_SEQ_122,                  "NA_BGM_SEQ_122.ogg",                  STREAM_BGM,     false },
};

// [BENS-STREAMED-AUDIO CONFIG END]
// -----------------------------------------------------------------------------

static void LoadAndBindStreamedSequence(ostSeqMap* spec) {
    s32 seqId;
    static unsigned char* modPath = NULL;

    if (modPath == NULL) {
        modPath = recomp_get_mod_file_path();
    }

    if (spec->kind == STREAM_FANFARE) {
        seqId = AudioApi_CreateStreamedFanfare(NULL, (char*)modPath, spec->file, spec->seqIO);
    } else {
        seqId = AudioApi_CreateStreamedBgm(NULL, (char*)modPath, spec->file, spec->seqIO);
    }

    if (seqId >= 0) {
        AudioApi_ReplaceSequence(spec->key, &gAudioCtx.sequenceTable->entries[seqId]);
        AudioApi_ReplaceSequenceFont(spec->key, 0, AudioApi_GetSequenceFont(seqId, 0));
        spec->replaced = true;
    }
}


static ostSeqMap* GetSpecBySeqId(s32 seqId) {
    int i;

    for (i = 0; i < ARRAY_COUNT(kSeqs); ++i) {
        if (kSeqs[i].replaced && kSeqs[i].key == seqId) {
            return &kSeqs[i];
        }
    }

    return NULL;
}

RECOMP_CALLBACK("magemods_audio_api", AudioApi_Init) void onAudioApiInit() {
    int i;

    for (i = 0; i < ARRAY_COUNT(kSeqs); ++i) {
        LoadAndBindStreamedSequence(&kSeqs[i]);
    }

    for (i = 0; i < CROSSFADE_DURATION_TICKS; i++) {
        fadeInCurve[i] = Math_SinF((f32)i / CROSSFADE_DURATION_TICKS * M_PI * 0.5f);
        fadeOutCurve[i] = Math_CosF((f32)i / CROSSFADE_DURATION_TICKS * M_PI * 0.5f);
    }

    // Set defaults from config on first load
    unsigned long volIdx = recomp_get_config_u32("remaster_volume");
    if (volIdx >= ARRAY_COUNT(kRemasterVolumeTable)) {
        volIdx = 1;
    }
    remasterVolumeMax = kRemasterVolumeTable[volIdx];

    activeChannel = (recomp_get_config_u32("default_soundtrack") != 0)
        ? OST_CHANNEL
        : REMASTER_CHANNEL;
}

static void ApplyDefaultSoundtrackConfig(void) {
    // Remaster volume: 0 = "-3 dB", 1 = "0 dB", 2 = "+3 dB"
    unsigned long volIdx = recomp_get_config_u32("remaster_volume");
    if (volIdx >= ARRAY_COUNT(kRemasterVolumeTable)) {
        volIdx = 1; // fallback to 0 dB
    }
    remasterVolumeMax = kRemasterVolumeTable[volIdx];

    // Reset to default on scene change: 0 = "Off", 1 = "On"
    if (recomp_get_config_u32("reset_on_scene_change") == 0) {
        return;
    }

    int configChannel = (recomp_get_config_u32("default_soundtrack") != 0)
        ? OST_CHANNEL
        : REMASTER_CHANNEL;

    if (activeChannel != configChannel) {
        activeChannel = configChannel;
        fadeTimer = CROSSFADE_DURATION_TICKS;

        if (activeChannel == REMASTER_CHANNEL) {
            Notifications_Emit("Ben's RST", "Active:", "REMASTER");
        } else {
            Notifications_Emit("Ben's RST", "Active:", "CD OST");
        }
    }
}

RECOMP_HOOK("Play_Init") void onPlayInit(GameState* gameState) {
    ApplyDefaultSoundtrackConfig();
}

RECOMP_HOOK("AudioScript_ProcessSequences") void onProcessSequences() {
    f32 fadeIn, fadeOut;

    if (fadeTimer > 0) {
        fadeIn = fadeInCurve[CROSSFADE_DURATION_TICKS - fadeTimer];
        fadeOut = fadeOutCurve[CROSSFADE_DURATION_TICKS - fadeTimer];
        fadeTimer--;
    } else {
        fadeIn = 1.0f;
        fadeOut = 0.0f;
    }

    if (activeChannel == REMASTER_CHANNEL) {
        remasterVolume = fadeIn * remasterVolumeMax;
        ostVolume = fadeOut * OST_VOLUME;
    } else {
        remasterVolume = fadeOut * remasterVolumeMax;
        ostVolume = fadeIn * OST_VOLUME;
    }
}

RECOMP_HOOK("AudioScript_SequencePlayerProcessSound") void onSequencePlayerProcessSound(SequencePlayer* seqPlayer) {
    s32 seqId;
    ostSeqMap* spec;
    SequenceChannel* channel;
    f32 volume;
    int i;

    seqId = AudioApi_GetSeqPlayerSeqId(seqPlayer);
    spec = GetSpecBySeqId(seqId);

    if (spec) {
        for (i = 0; i < ARRAY_COUNT(seqPlayer->channels); i++) {
            channel = seqPlayer->channels[i];
            if (channel == NULL) {
                continue;
            }

            // All even channels are remaster channels, all odd channels are OST channels
            volume = (i % 2) == REMASTER_CHANNEL
                ? remasterVolume
                : ostVolume;

            if (channel->volume != volume) {
                channel->volume = volume;
                channel->changes.s.volume = true;
            }
        }
    }

}

RECOMP_HOOK("Graph_ExecuteAndDraw") void onGraphExecuteAndDraw(GraphicsContext* gfxCtx, GameState* gameState) {
    // Quick switch with L: 0 = "On", 1 = "Off"
    if (recomp_get_config_u32("quick_switch_l") == 0 &&
        CHECK_BTN_ALL(CONTROLLER1(gameState)->press.button, BTN_L)) {
        activeChannel = (activeChannel + 1) % 2;
        fadeTimer = CROSSFADE_DURATION_TICKS;

        if (activeChannel == REMASTER_CHANNEL) {
            Notifications_Emit("Ben's RST", "Active:", "REMASTER");
        } else {
            Notifications_Emit("Ben's RST", "Active:", "CD OST");
        }
    }

}
