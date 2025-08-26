// ImageDriver.cpp
#include "image_driver.hpp"

ImageDriver::ImageDriver() {}
ImageDriver::~ImageDriver() { shutdown(); }

bool ImageDriver::init() {
    if (inited_) return true;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return false;
    }
    // Create a renderer/window lazily in display to allow multiple windows; keep one default renderer.
    // We'll create window+renderer in display().
    inited_ = true;
    return true;
}

void ImageDriver::shutdown() {
    releaseAll();
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    if (inited_) {
        IMG_Quit();
        SDL_Quit();
        inited_ = false;
    }
}

int ImageDriver::createPictureFromSurface(SDL_Surface* surf, const std::string &path) {
    if (!surf) return -1;
    if (!inited_) {
        std::cerr << "ImageDriver: not initialized\n";
        SDL_FreeSurface(surf);
        return -1;
    }

    // Ensure we have a temporary renderer+window if none exist
    bool createdLocalWindow = false;
    if (!renderer_) {
        window_ = SDL_CreateWindow("CRTZ Image (temp)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, surf->w, surf->h, SDL_WINDOW_HIDDEN);
        if (!window_) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            SDL_FreeSurface(surf);
            return -1;
        }
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow(window_); window_ = nullptr;
            SDL_FreeSurface(surf);
            return -1;
        }
        createdLocalWindow = true;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);

    if (!tex) {
        std::cerr << "CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
        if (createdLocalWindow) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; SDL_DestroyWindow(window_); window_ = nullptr; }
        return -1;
    }

    Picture p;
    p.tex = tex;
    p.w = w;
    p.h = h;
    p.path = path;
    pictures_.push_back(p);
    int idx = (int)pictures_.size() - 1;

    // if we created temp renderer+window, keep renderer but hide window until display
    if (createdLocalWindow) {
        // don't destroy renderer/window here; reuse for displayByIndex
        // we created a hidden window to have a renderer available
    }
    return idx;
}

int ImageDriver::loadImage(const std::string &path) {
    if (!init()) return -1;

    SDL_Surface* surf = IMG_Load(path.c_str());
    if (!surf) {
        std::cerr << "IMG_Load failed for '" << path << "': " << IMG_GetError() << std::endl;
        return -1;
    }
    return createPictureFromSurface(surf, path);
}

std::vector<int> ImageDriver::loadFolder(const std::string &folderPath) {
    std::vector<int> result;
    if (!init()) return result;

    try {
        namespace fs = std::filesystem;
        if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
            std::cerr << "loadFolder: folder does not exist: " << folderPath << std::endl;
            return result;
        }

        std::vector<std::filesystem::directory_entry> entries;
        for (auto &e : std::filesystem::directory_iterator(folderPath)) {
            if (e.is_regular_file()) {
                std::string name = e.path().filename().string();
                if (isImageExtension(name)) entries.push_back(e);
            }
        }

        // Sort entries for deterministic order
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.path().filename().string() < b.path().filename().string();
        });

        for (auto &e : entries) {
            int idx = loadImage(e.path().string());
            if (idx >= 0) result.push_back(idx);
        }
    } catch (std::exception &ex) {
        std::cerr << "loadFolder exception: " << ex.what() << std::endl;
    }
    return result;
}

bool ImageDriver::display(const std::string &path) {
    int idx = loadImage(path);
    if (idx < 0) return false;
    bool ok = displayByIndex(idx);
    // release the temporarly loaded picture after display
    releasePicture(idx);
    return ok;
}

bool ImageDriver::displayByIndex(int index) {
    if (index < 0 || index >= (int)pictures_.size()) {
        std::cerr << "displayByIndex: invalid index " << index << std::endl;
        return false;
    }
    if (!init()) return false;

    Picture &p = pictures_[index];
    // create window (or reuse) sized to image if requested
    int winW = p.w, winH = p.h;
    if (!window_) {
        window_ = SDL_CreateWindow(("CRTZ: " + p.path).c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
        if (!window_) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            return false;
        }
    } else {
        SDL_SetWindowTitle(window_, ("CRTZ: " + p.path).c_str());
        SDL_SetWindowSize(window_, winW, winH);
        SDL_ShowWindow(window_);
    }

    if (!renderer_) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    // Render loop (blocking) until window closed
    bool quit = false;
    SDL_Event e;
    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            // optionally accept key press to close
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit = true;
        }

        SDL_RenderClear(renderer_);
        SDL_Rect dst = {0,0, winW, winH};
        if (!scaleToImage_) {
            // scale to current window size
            int cw, ch;
            SDL_GetWindowSize(window_, &cw, &ch);
            dst.w = cw; dst.h = ch;
        }
        SDL_RenderCopy(renderer_, p.tex, nullptr, &dst);
        SDL_RenderPresent(renderer_);
        SDL_Delay(10);
    }

    // Optionally hide window instead of destroying to allow next display faster:
    SDL_HideWindow(window_);
    return true;
}

void ImageDriver::releasePicture(int index) {
    if (index < 0 || index >= (int)pictures_.size()) return;
    if (pictures_[index].tex) {
        SDL_DestroyTexture(pictures_[index].tex);
        pictures_[index].tex = nullptr;
    }
    // optional: clear slot (we'll keep vector size for index stability)
    pictures_[index].path.clear();
    pictures_[index].w = pictures_[index].h = 0;
}

void ImageDriver::releaseAll() {
    for (auto &p : pictures_) {
        if (p.tex) {
            SDL_DestroyTexture(p.tex);
            p.tex = nullptr;
        }
    }
    pictures_.clear();
}
