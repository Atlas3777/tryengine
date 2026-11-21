options gen2
options relaxed_assign = false

module Boot shared public

//! Game boot flow: loading screen, asynchronous content request and entry into the
//! startup scene.
//!
//! The loader never applies a scene and never knows about a loading screen. This module
//! owns that sequence for the game, keeps the startup policy in one place and leaves
//! `Game.das` with gameplay only.
//!
//! The flow is polled from `Update`, like the loader itself: no boot coroutine and no
//! stop flag. The editor's `PlayMode.EndPlay` only aborts the loader; a text read left
//! pending by a stopped session is released by the next `Start`.

require tryengine/Assets
require tryengine/Content
require tryengine/Input
require tryengine/Loader
require tryengine/Scene

// === startup policy ===
// A new project changes these paths; C++ loads no scene and preloads no content.
// For a standalone build, select both .scene files in BuildSettings: only selected
// scenes get their own pak. Prefabs, bundles and data files are in the common pak.
let STARTUP_SCENE = "content/test.scene"
let LOADING_SCENE = "content/loading.scene"
let STARTUP_BUNDLE = "data/bundles/startup.bundle"

// Purely a feel knob. The loader reports readiness as soon as the assets are there;
// this holds the loading screen for at least this long after that.
let MIN_LOADING_SECONDS = 1.5f

// false: enter the startup scene as soon as it is ready.
// true:  hold the ready loading screen until Space.
let WAIT_FOR_SPACE = true

enum private BootPhase {
    Idle
    ReadingLoadingScene
    Loading
    Holding
    Playing
    Failed
}

var private g_phase = BootPhase.Idle
var private g_on_playing : SceneCallback = @@BootNoop
var private g_loading_text_id : uint64 = 0ul
var private g_content_ready = false
var private g_ready_at = 0.0d

def private BootNoop() {
}

//! Starts the boot flow. `on_playing` runs after the startup scene is applied, when
//! gameplay sets up its own entities. Call once from `GameStart`.
def public Start(on_playing : SceneCallback) {
    CancelPendingText()
    g_phase = BootPhase.ReadingLoadingScene
    g_on_playing = on_playing
    g_content_ready = false
    g_ready_at = 0.0d
    g_loading_text_id = Assets::RequestText(LOADING_SCENE)
    if (g_loading_text_id == 0ul) {
        FailBoot("loading scene '{LOADING_SCENE}' is not available")
    }
}

//! One frame of the boot flow. Returns true once gameplay owns the frame.
def public Update(now : double) : bool {
    if (g_phase == BootPhase.Idle || g_phase == BootPhase.Failed) {
        return false
    }
    if (g_phase == BootPhase.Playing) {
        return true
    }
    if (g_phase == BootPhase.ReadingLoadingScene) {
        ApplyLoadingScene()
        return false
    }

    if (g_phase == BootPhase.Loading) {
        Loader::Update()
        if (Loader::IsFailed()) {
            FailBoot(Loader::Error())
            return false
        }
        if (!g_content_ready) {
            return false
        }
        g_phase = BootPhase.Holding
        g_ready_at = now
    }

    if (g_phase == BootPhase.Holding) {
        if (now - g_ready_at < double(MIN_LOADING_SECONDS)) {
            return false
        }
        if (WAIT_FOR_SPACE && !pressed(Key.Space)) {
            return false
        }
        // Apply the preloaded startup scene. Its callback is the scene-specific
        // gameplay hook; the loader releases its references only after the scene exists.
        if (!Scene::Load(STARTUP_SCENE, g_on_playing)) {
            FailBoot("startup scene '{STARTUP_SCENE}' failed to apply")
            return false
        }
        Loader::Finish()
        g_phase = BootPhase.Playing
    }

    return g_phase == BootPhase.Playing
}

// Polls the loading-scene text. Only this step is asynchronous; once the text is here,
// the world swap and the loader request are synchronous.
def private ApplyLoadingScene() {
    if (g_loading_text_id == 0ul) {
        FailBoot("loading scene '{LOADING_SCENE}' is not available")
    } elif (Assets::TextIsFailed(g_loading_text_id)) {
        FailBoot("loading scene '{LOADING_SCENE}' is not available")
    } elif (Assets::TextIsReady(g_loading_text_id)) {
        let text = Assets::TakeText(g_loading_text_id)
        g_loading_text_id = 0ul
        if (empty(text)) {
            FailBoot("loading scene '{LOADING_SCENE}' is empty")
        } else {
            Content::Cache(ContentKind.Scene, LOADING_SCENE, text)
            if (!Scene::Load(LOADING_SCENE)) {
                FailBoot("loading scene '{LOADING_SCENE}' failed to apply")
            } else {
                var request <- LoaderRequest()
                request.SetBlocking(true)
                request.AddBundle(STARTUP_BUNDLE)
                request.AddScene(STARTUP_SCENE)

                if (!Loader::Invoke(request, @@OnContentReady)) {
                    FailBoot("content request could not be started")
                } else {
                    g_phase = BootPhase.Loading
                }
            }
        }
    }
}

// Loader callback: the request reached `Ready`. Entering the target scene is still the
// game's decision (minimum screen time / Space), so this only records readiness.
def private OnContentReady() {
    g_content_ready = true
}

def private CancelPendingText() {
    if (g_loading_text_id != 0ul) {
        Assets::CancelText(g_loading_text_id)
        g_loading_text_id = 0ul
    }
}

def private FailBoot(reason : string) {
    CancelPendingText()
    g_phase = BootPhase.Failed
    print("[BOOT ERROR] {reason}\n")
    Loader::Abort()
}
