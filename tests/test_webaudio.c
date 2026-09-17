/*
 * Copyright (c) 2026 General Arcade (Pte. Ltd.)
 * SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-GeneralArcade-Commercial
 */

#include "audio/audio_engine.h"
#include "bindings/bind_audio.h"
#include "bindings/bind_io.h"
#include "io/file_io.h"
#include "engine/js_engine.h"

#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Minimal test framework */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [%d] %s ... ", tests_run, #name); \
    name(); \
    if (tests_failed >= tests_run - tests_passed) { \
        /* test already marked as failed */ \
    } else { \
        printf("PASS\n"); \
        tests_passed++; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR(result, expected) do { \
    if (!(result) || strcmp((result), (expected)) != 0) { \
        printf("FAIL\n    expected \"%s\", got \"%s\"\n    at %s:%d\n", \
               (expected), (result) ? (result) : "NULL", __FILE__, __LINE__); \
        tests_failed++; \
        if (result) js_engine_free_string(result); \
        return; \
    } \
    js_engine_free_string(result); \
} while(0)

/* WAV file generator for test fixtures */

static uint8_t *generate_wav(float freq_hz, float duration_sec,
                              int sample_rate, size_t *out_size)
{
    int num_samples = (int)(sample_rate * duration_sec);
    int data_size = num_samples * 2;
    int file_size = 44 + data_size;

    uint8_t *buf = (uint8_t *)calloc(1, file_size);
    if (!buf) return NULL;

    memcpy(buf, "RIFF", 4);
    int chunk_size = file_size - 8;
    buf[4] = chunk_size & 0xFF;
    buf[5] = (chunk_size >> 8) & 0xFF;
    buf[6] = (chunk_size >> 16) & 0xFF;
    buf[7] = (chunk_size >> 24) & 0xFF;
    memcpy(buf + 8, "WAVE", 4);

    memcpy(buf + 12, "fmt ", 4);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
    buf[20] = 1; buf[21] = 0;
    buf[22] = 1; buf[23] = 0;
    buf[24] = sample_rate & 0xFF;
    buf[25] = (sample_rate >> 8) & 0xFF;
    buf[26] = (sample_rate >> 16) & 0xFF;
    buf[27] = (sample_rate >> 24) & 0xFF;
    int byte_rate = sample_rate * 2;
    buf[28] = byte_rate & 0xFF;
    buf[29] = (byte_rate >> 8) & 0xFF;
    buf[30] = (byte_rate >> 16) & 0xFF;
    buf[31] = (byte_rate >> 24) & 0xFF;
    buf[32] = 2; buf[33] = 0;
    buf[34] = 16; buf[35] = 0;

    memcpy(buf + 36, "data", 4);
    buf[40] = data_size & 0xFF;
    buf[41] = (data_size >> 8) & 0xFF;
    buf[42] = (data_size >> 16) & 0xFF;
    buf[43] = (data_size >> 24) & 0xFF;

    int16_t *samples = (int16_t *)(buf + 44);
    for (int i = 0; i < num_samples; i++) {
        double t = (double)i / sample_rate;
        double val = sin(2.0 * 3.14159265358979 * freq_hz * t);
        samples[i] = (int16_t)(val * 32000.0);
    }

    *out_size = file_size;
    return buf;
}

static int write_wav_fixture(const char *path, float freq_hz, float duration_sec)
{
    size_t size;
    uint8_t *buf = generate_wav(freq_hz, duration_sec, 44100, &size);
    if (!buf) return 0;

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t written = fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    return written == size;
}

/* Test setup / teardown */

static const char *TEST_WAV = "tests/fixtures/test_audio_short.wav";
static const char *TEST_WAV_LONG = "tests/fixtures/test_audio_long.wav";

static void setup_fixtures(void)
{
    system("mkdir -p tests/fixtures");

    FILE *f = fopen(TEST_WAV, "rb");
    if (!f) {
        write_wav_fixture(TEST_WAV, 440.0f, 0.1f);
    } else {
        fclose(f);
    }

    f = fopen(TEST_WAV_LONG, "rb");
    if (!f) {
        write_wav_fixture(TEST_WAV_LONG, 261.63f, 1.0f);
    } else {
        fclose(f);
    }
}

static JSEngine *s_js = NULL;

static void js_setup(void)
{
    s_js = js_engine_init();
    bind_io_register(js_engine_get_context(s_js));
    bind_audio_register(js_engine_get_context(s_js));
    file_io_set_game_root(".");

    /* DOM shim must precede the WebAudio shim (provides window, document). */
    js_engine_eval_file(s_js, "src/shims/dom_shim.js");
    js_engine_execute_pending_jobs(s_js);

    js_engine_eval_file(s_js, "src/shims/webaudio_shim.js");
    js_engine_execute_pending_jobs(s_js);
}

static void js_teardown(void)
{
    /* Shut down native audio if the shim initialized it. */
    js_engine_eval(s_js, "if (__native_audio.isInitialized()) __native_audio.shutdown()", "<teardown>");
    js_engine_shutdown(s_js);
    s_js = NULL;
}

static int js_check(const char *script)
{
    char *result = js_engine_eval_string(s_js, script, "<test>");
    if (!result) return 0;
    int ok = (strcmp(result, "true") == 0);
    if (!ok) {
        printf("(got: %s) ", result);
    }
    js_engine_free_string(result);
    return ok;
}

/* Tests: AudioContext creation and properties */

TEST(audiocontext_exists)
{
    js_setup();
    ASSERT(js_check("typeof AudioContext === 'function'"));
    ASSERT(js_check("typeof webkitAudioContext === 'function'"));
    js_teardown();
}

TEST(audiocontext_window_globals)
{
    js_setup();
    ASSERT(js_check("window.AudioContext === AudioContext"));
    ASSERT(js_check("window.webkitAudioContext === AudioContext"));
    js_teardown();
}

TEST(audiocontext_create)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "ctx !== null && ctx !== undefined"
    ));
    ASSERT(js_check("ctx.sampleRate === 44100"));
    ASSERT(js_check("ctx.state === 'running'"));
    ASSERT(js_check("ctx.destination !== null"));
    ASSERT(js_check("typeof ctx.currentTime === 'number'"));
    ASSERT(js_check("ctx.currentTime >= 0"));
    js_teardown();
}

TEST(audiocontext_resume)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var resolved = false;"
        "ctx.resume().then(function() { resolved = true; });"
        "true"
    ));
    js_engine_execute_pending_jobs(s_js);
    ASSERT(js_check("resolved === true"));
    ASSERT(js_check("ctx.state === 'running'"));
    js_teardown();
}

TEST(audiocontext_create_nodes)
{
    js_setup();
    js_engine_eval(s_js, "var ctx = new AudioContext();", "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("var gain = ctx.createGain(); gain !== null"));
    ASSERT(js_check("gain.gain !== null && gain.gain !== undefined"));
    ASSERT(js_check("gain.gain.value === 1.0"));

    ASSERT(js_check("var panner = ctx.createPanner(); panner !== null"));
    ASSERT(js_check("panner.panningModel === 'equalpower'"));

    ASSERT(js_check("var src = ctx.createBufferSource(); src !== null"));
    ASSERT(js_check("src.playbackRate !== null"));
    ASSERT(js_check("src.playbackRate.value === 1.0"));
    ASSERT(js_check("src.loop === false"));

    js_teardown();
}

/* Tests: Audio graph connection */

TEST(audio_graph_connect)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var gain = ctx.createGain();"
        "var panner = ctx.createPanner();"
        "var source = ctx.createBufferSource();"
        "source.connect(gain);"
        "gain.connect(panner);"
        "panner.connect(ctx.destination);"
        "source._connectedTo === gain"
    ));
    ASSERT(js_check("gain._connectedTo === panner"));
    ASSERT(js_check("panner._connectedTo === ctx.destination"));
    js_teardown();
}

/* Tests: GainNode parameter control */

TEST(gain_set_value_at_time)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var gain = ctx.createGain();"
        "gain.gain.setValueAtTime(0.5, ctx.currentTime);"
        "gain.gain.value === 0.5"
    ));
    js_teardown();
}

TEST(gain_linear_ramp)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var gain = ctx.createGain();"
        "gain.gain.setValueAtTime(0, ctx.currentTime);"
        "gain.gain.linearRampToValueAtTime(1.0, ctx.currentTime + 1.0);"
        "true"
    ));
    js_teardown();
}

/* Tests: decodeAudioData */

TEST(decode_audio_data)
{
    js_setup();

    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_short.wav');"
        "wavData !== null && wavData.byteLength > 0"
    ));

    js_engine_eval(s_js,
        "var decodedBuffer = null;"
        "var decodeError = null;"
        "ctx.decodeAudioData(wavData).then(function(buf) {"
        "    decodedBuffer = buf;"
        "}).catch(function(e) {"
        "    decodeError = e;"
        "});",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("decodeError === null"));
    ASSERT(js_check("decodedBuffer !== null"));
    ASSERT(js_check("decodedBuffer instanceof AudioBuffer"));
    ASSERT(js_check("decodedBuffer.duration > 0"));
    ASSERT(js_check("decodedBuffer._nativeHandle > 0"));

    js_teardown();
}

TEST(decode_audio_data_callback)
{
    js_setup();

    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_short.wav');"
        "wavData !== null"
    ));

    /* Callback-style decodeAudioData. */
    js_engine_eval(s_js,
        "var cbBuffer = null;"
        "var cbError = null;"
        "ctx.decodeAudioData(wavData,"
        "    function(buf) { cbBuffer = buf; },"
        "    function(e) { cbError = e; }"
        ");",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("cbError === null"));
    ASSERT(js_check("cbBuffer !== null"));
    ASSERT(js_check("cbBuffer.duration > 0"));

    js_teardown();
}

/* Tests: Full playback chain (AudioContext -> decode -> play) */

TEST(full_playback_chain)
{
    js_setup();

    /* RPG Maker MZ audio graph:
       BufferSource -> GainNode -> PannerNode -> MasterGainNode -> destination */
    js_engine_eval(s_js,
        "var ctx = new AudioContext();"
        "var masterGain = ctx.createGain();"
        "masterGain.gain.setValueAtTime(0.8, ctx.currentTime);"
        "masterGain.connect(ctx.destination);"
        ""
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_long.wav');"
        "var playBuffer = null;"
        "ctx.decodeAudioData(wavData).then(function(buf) {"
        "    playBuffer = buf;"
        "});",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("playBuffer !== null"));

    js_engine_eval(s_js,
        "var panner = ctx.createPanner();"
        "panner.panningModel = 'equalpower';"
        "panner.connect(masterGain);"
        ""
        "var gainNode = ctx.createGain();"
        "gainNode.gain.setValueAtTime(0.7, ctx.currentTime);"
        "gainNode.connect(panner);"
        ""
        "var sourceNode = ctx.createBufferSource();"
        "sourceNode.buffer = playBuffer;"
        "sourceNode.loop = true;"
        "sourceNode.playbackRate.setValueAtTime(1.0, ctx.currentTime);"
        "sourceNode.connect(gainNode);"
        "sourceNode.start(0, 0);",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("sourceNode._voiceHandle > 0"));
    ASSERT(js_check("sourceNode._started === true"));

    js_engine_eval(s_js,
        "gainNode.gain.setValueAtTime(0.3, ctx.currentTime);",
        "<test>");

    js_engine_eval(s_js,
        "panner.setPosition(0.5, 0, 0.866);",
        "<test>");

    js_engine_eval(s_js, "sourceNode.stop();", "<test>");
    ASSERT(js_check("sourceNode._stopped === true"));

    js_teardown();
}

/* Tests: AudioBuffer properties */

TEST(audio_buffer_properties)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "var buf = ctx.createBuffer(2, 44100, 44100);"
        "buf.numberOfChannels === 2"
    ));
    ASSERT(js_check("buf.length === 44100"));
    ASSERT(js_check("buf.sampleRate === 44100"));
    ASSERT(js_check("buf.duration === 1.0"));
    ASSERT(js_check("buf.getChannelData(0) instanceof Float32Array"));
    js_teardown();
}

/* Tests: onended async behavior */

TEST(onended_called_async)
{
    js_setup();

    js_engine_eval(s_js,
        "var ctx = new AudioContext();"
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_short.wav');"
        "var buf = null;"
        "ctx.decodeAudioData(wavData).then(function(b) { buf = b; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);
    ASSERT(js_check("buf !== null"));

    js_engine_eval(s_js,
        "var endedCalled = false;"
        "var g = ctx.createGain();"
        "g.connect(ctx.destination);"
        "var src = ctx.createBufferSource();"
        "src.buffer = buf;"
        "src.connect(g);"
        "src.onended = function() { endedCalled = true; };"
        "src.start(0, 0);",
        "<test>");
    js_engine_execute_pending_jobs(s_js);
    ASSERT(js_check("src._started === true"));

    /* onended must not fire synchronously from stop(); only after the
       microtask queue is flushed. */
    js_engine_eval(s_js, "src.stop();", "<test>");
    ASSERT(js_check("src._stopped === true"));
    ASSERT(js_check("endedCalled === false"));

    js_engine_execute_pending_jobs(s_js);
    ASSERT(js_check("endedCalled === true"));

    js_teardown();
}

/* Tests: Html5Audio fallback stub */

TEST(html5audio_exists)
{
    js_setup();
    ASSERT(js_check("typeof Html5Audio === 'object'"));
    ASSERT(js_check("typeof Html5Audio.setup === 'function'"));
    ASSERT(js_check("typeof Html5Audio.play === 'function'"));
    ASSERT(js_check("typeof Html5Audio.stop === 'function'"));
    ASSERT(js_check("typeof Html5Audio.fadeIn === 'function'"));
    ASSERT(js_check("typeof Html5Audio.fadeOut === 'function'"));
    ASSERT(js_check("Html5Audio.isReady() === false"));
    ASSERT(js_check("Html5Audio.isError() === false"));
    ASSERT(js_check("Html5Audio.isPlaying() === false"));
    js_teardown();
}

/* Tests: RPG Maker MZ WebAudio class simulation */

TEST(rmmz_webaudio_pattern)
{
    js_setup();

    /* Mirrors WebAudio.initialize() -> _createContext() -> _createMasterGainNode(). */
    ASSERT(js_check(
        "var _context = null;"
        "var _masterGainNode = null;"
        "var _masterVolume = 1;"
        ""
        "var AC = window.AudioContext || window.webkitAudioContext;"
        "_context = new AC();"
        "_context !== null"
    ));

    ASSERT(js_check(
        "_masterGainNode = _context.createGain();"
        "_masterGainNode.gain.setValueAtTime(_masterVolume, _context.currentTime);"
        "_masterGainNode.connect(_context.destination);"
        "_masterGainNode !== null"
    ));

    ASSERT(js_check(
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_short.wav');"
        "wavData !== null"
    ));

    js_engine_eval(s_js,
        "var testBuf = null;"
        "_context.decodeAudioData(wavData.slice())"
        "    .then(function(buffer) { testBuf = buffer; })"
        "    .catch(function() { testBuf = 'error'; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("testBuf !== null && testBuf !== 'error'"));
    ASSERT(js_check("testBuf.duration > 0"));

    /* Mirrors _startPlaying(). */
    js_engine_eval(s_js,
        "var _pannerNode = _context.createPanner();"
        "_pannerNode.panningModel = 'equalpower';"
        "_pannerNode.connect(_masterGainNode);"
        ""
        "var _gainNode = _context.createGain();"
        "_gainNode.gain.setValueAtTime(0.8, _context.currentTime);"
        "_gainNode.connect(_pannerNode);"
        ""
        "var _sourceNode = _context.createBufferSource();"
        "_sourceNode.buffer = testBuf;"
        "_sourceNode.loop = false;"
        "_sourceNode.playbackRate.setValueAtTime(1.0, _context.currentTime);"
        "_sourceNode.connect(_gainNode);"
        "_sourceNode.start(0, 0);",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("_sourceNode._started === true"));
    ASSERT(js_check("_sourceNode._voiceHandle > 0"));

    /* Fade-out pattern. */
    js_engine_eval(s_js,
        "var curTime = _context.currentTime;"
        "_gainNode.gain.setValueAtTime(0.8, curTime);"
        "_gainNode.gain.linearRampToValueAtTime(0, curTime + 0.5);",
        "<test>");

    js_engine_eval(s_js,
        "_pannerNode.setPosition(0.3, 0, 0.954);",
        "<test>");

    js_engine_eval(s_js, "_sourceNode.stop();", "<test>");
    ASSERT(js_check("_sourceNode._stopped === true"));

    js_teardown();
}

/* Tests: Context state management */

TEST(context_state_suspended_resume)
{
    js_setup();
    ASSERT(js_check(
        "var ctx = new AudioContext();"
        "ctx.state === 'running'"
    ));

    /* RPG Maker MZ resumes a suspended context on user gesture. */
    js_engine_eval(s_js,
        "var resumeResult = false;"
        "ctx.state = 'suspended';"
        "ctx.resume().then(function() { resumeResult = true; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("resumeResult === true"));
    ASSERT(js_check("ctx.state === 'running'"));

    js_teardown();
}

/* Tests: Multiple simultaneous source nodes */

TEST(multiple_source_nodes)
{
    js_setup();

    js_engine_eval(s_js,
        "var ctx = new AudioContext();"
        "var wavData = __native_io.readFileBinary('tests/fixtures/test_audio_short.wav');"
        "var buf = null;"
        "ctx.decodeAudioData(wavData).then(function(b) { buf = b; });",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("buf !== null"));

    js_engine_eval(s_js,
        "var sources = [];"
        "for (var i = 0; i < 3; i++) {"
        "    var g = ctx.createGain();"
        "    g.connect(ctx.destination);"
        "    var s = ctx.createBufferSource();"
        "    s.buffer = buf;"
        "    s.connect(g);"
        "    s.start(0, 0);"
        "    sources.push(s);"
        "}",
        "<test>");
    js_engine_execute_pending_jobs(s_js);

    ASSERT(js_check("sources.length === 3"));
    ASSERT(js_check("sources[0]._started === true"));
    ASSERT(js_check("sources[1]._started === true"));
    ASSERT(js_check("sources[2]._started === true"));
    ASSERT(js_check("sources[0]._voiceHandle > 0"));

    js_engine_eval(s_js,
        "for (var i = 0; i < sources.length; i++) {"
        "    try { sources[i].stop(); } catch(e) {}"
        "}",
        "<test>");

    js_teardown();
}

/* Main */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("=== WebAudio Shim Tests ===\n");

    setup_fixtures();

    printf("\n--- AudioContext tests ---\n");
    RUN(audiocontext_exists);
    RUN(audiocontext_window_globals);
    RUN(audiocontext_create);
    RUN(audiocontext_resume);
    RUN(audiocontext_create_nodes);

    printf("\n--- Audio graph tests ---\n");
    RUN(audio_graph_connect);
    RUN(gain_set_value_at_time);
    RUN(gain_linear_ramp);

    printf("\n--- decodeAudioData tests ---\n");
    RUN(decode_audio_data);
    RUN(decode_audio_data_callback);

    printf("\n--- Playback tests ---\n");
    RUN(full_playback_chain);
    RUN(audio_buffer_properties);
    RUN(multiple_source_nodes);

    printf("\n--- onended async tests ---\n");
    RUN(onended_called_async);

    printf("\n--- Html5Audio tests ---\n");
    RUN(html5audio_exists);

    printf("\n--- RPG Maker MZ compatibility tests ---\n");
    RUN(rmmz_webaudio_pattern);
    RUN(context_state_suspended_resume);

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
