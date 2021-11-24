//#define LOG_NDEBUG 0
#define LOG_TAG "native_mediaplayer"
#include <utils/Log.h>

#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <media/IStreamSource.h>
#include <media/mediaplayer.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>

#include <media/IMediaPlayerService.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/Surface.h>

#include <fcntl.h>
#include <ui/DisplayMode.h>


using namespace android;

////////////////////////////////////////////////////////////////////////////////

struct MyClient : public BnMediaPlayerClient {
    MyClient()
        : mEOS(false) {
    }

    virtual void notify(int msg, int ext1 __unused, int ext2 __unused, const Parcel *obj __unused) {
        Mutex::Autolock autoLock(mLock);

        if (msg == MEDIA_ERROR || msg == MEDIA_PLAYBACK_COMPLETE) {
            mEOS = true;
            mCondition.signal();
        }
    }

    void waitForEOS() {
        Mutex::Autolock autoLock(mLock);
        while (!mEOS) {
            mCondition.wait(mLock);
        }
    }

protected:
    virtual ~MyClient() {
    }

private:
    Mutex mLock;
    Condition mCondition;

    bool mEOS;

    DISALLOW_EVIL_CONSTRUCTORS(MyClient);
};

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
    if (argc < 2){
        printf("ERROR: must input the video path.\n");
        return 1;
    }

    printf("play video with path: %s\n", argv[1]);

    // Start Binder thread pool.  MediaPlayer needs to be able to receive
    // messages from mediaserver.
    ProcessState::self()->startThreadPool();

    sp<SurfaceComposerClient> composerClient;
    sp<SurfaceControl> control;
    sp<Surface> surface;
    int32_t position = argc > 2 ? atoi(argv[2]) : 1;

    {
        /* create surface */
        composerClient = new SurfaceComposerClient;
        CHECK_EQ(composerClient->initCheck(), (status_t)OK);

        const sp<IBinder> display = SurfaceComposerClient::getInternalDisplayToken();
        CHECK(display != nullptr);

        ui::DisplayMode mode;
        CHECK_EQ(SurfaceComposerClient::getActiveDisplayMode(display, &mode), NO_ERROR);

        const ui::Size& resolution = mode.resolution;
        const ssize_t displayWidth = resolution.getWidth();
        const ssize_t displayHeight = resolution.getHeight();

        /* surface display positon */
        int32_t offsetx, offsety, width, height;

        width = displayWidth / 2;
        height = displayHeight / 4;

        switch (position) {
        case 1: {
            offsetx = 0;
            offsety = 0;
        } break;
        case 2: {
            offsetx = displayWidth / 2;
            offsety = 0;
        } break;
        case 3: {
            offsetx = 0;
            offsety = displayHeight / 4;
        } break;
        case 4: {
            offsetx = displayWidth / 2;
            offsety = displayHeight / 4;
        } break;
        case 5: {
            offsetx = 0;
            offsety = displayHeight / 2;
        } break;
        case 6: {
            offsetx = displayWidth / 2;
            offsety = displayHeight / 2;
        } break;
        case 7: {
            offsetx = 0;
            offsety = displayHeight / 4 * 3;
        } break;
        case 8: {
            offsetx = displayWidth / 2;
            offsety = displayHeight / 4 * 3;
        } break;
        default:
            offsetx = 0;
            offsety = 0;
            break;
        }

        printf("renderSurface - display is %zd x %zd\n", displayWidth, displayHeight);

        control = composerClient->createSurface(
                String8("A Surface"),
                width,
                height,
                PIXEL_FORMAT_RGB_565,
                0);

        CHECK(control != NULL);
        CHECK(control->isValid());

        SurfaceComposerClient::Transaction{}
                .setPosition(control, offsetx, offsety)
                .setLayer(control, INT_MAX)
                .show(control)
                .apply();

        surface = control->getSurface();
        CHECK(surface != NULL);
    }

    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("media.player"));
    sp<IMediaPlayerService> service = interface_cast<IMediaPlayerService>(binder);

    CHECK(service.get() != NULL);

    sp<MyClient> client = new MyClient;

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "failed to open file '%s'.\n", argv[1]);
        return 1;
    }

    sp<IMediaPlayer> player =
        service->create(client, AUDIO_SESSION_ALLOCATE);

    if (player != NULL && player->setDataSource(fd, 0, 0x7ffffffffffffffL) == NO_ERROR) {
        player->setVideoSurfaceTexture(surface->getIGraphicBufferProducer());
        player->prepareAsync();
        player->setLooping(1);
        player->start();

        client->waitForEOS();

        player->stop();
    } else {
        fprintf(stderr, "failed to instantiate player.\n");
    }

    composerClient->dispose();
}

