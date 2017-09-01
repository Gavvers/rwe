#ifndef RWE_MENUCONTEXT_H
#define RWE_MENUCONTEXT_H

#include <memory>
#include <rwe/AudioService.h>
#include <rwe/UiPanelScene.h>
#include <rwe/tdf/SimpleTdfAdapter.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>

#include "SceneManager.h"

namespace rwe
{
    class Controller
    {
    private:
        AbstractVirtualFileSystem* vfs;
        SceneManager* sceneManager;
        TdfBlock* allSoundTdf;
        AudioService* audioService;
        TextureService* textureService;

        UiFactory uiFactory;
        std::shared_ptr<UiPanelScene> scene;

        AudioService::LoopToken bgmHandle;
    public:

        Controller(AbstractVirtualFileSystem *vfs, SceneManager *sceneManager,
                   TdfBlock *allSoundTdf, AudioService *audioService, TextureService* textureService);

        void goToMainMenu();

        void goToSingleMenu();

        void goToSkirmishMenu();

        void goToPreviousMenu();

        void start();

        void exit();

        void message(const std::string& topic, const std::string& message);
    };
}


#endif
