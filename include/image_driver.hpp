// ImageDriver.h
#pragma once

#include "SDL2/SDL.h"
#include "SDL2/SDL_image.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <iostream>

struct Picture {
    SDL_Texture* tex = nullptr;
    int w = 0;
    int h = 0;
    std::string path;
};

class ImageDriver {
public:
    ImageDriver();
    ~ImageDriver();

    // Initialize/Shutdown
    bool init();              // call once before use
    void shutdown();          // cleans up

    // Load single image from path, returns index (>=0) or -1 on error
    int loadImage(const std::string &path);

    // Load all images in folder (non-recursive), returns vector of indices
    std::vector<int> loadFolder(const std::string &folderPath);

    // Display by path (loads temporarily if necessary). Blocks until window closed.
    bool display(const std::string &path);

    // Display by preloaded index (from loadImage/loadFolder). Blocks until closed.
    bool displayByIndex(int index);

    // Release a specific picture (frees the texture)
    void releasePicture(int index);

    // Release all pictures
    void releaseAll();

    // Option: set default window size behavior
    void setScaleToImage(bool scaleToImage) { scaleToImage_ = scaleToImage; }

   
    bool isInitialized() const { return inited_; }


private:
    bool inited_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<Picture> pictures_;
    bool scaleToImage_ = true;

    static inline bool isImageExtension(const std::string &name) {
        static const std::vector<std::string> exts = { ".png", ".jpg", ".jpeg", ".bmp", ".gif" };
        std::string s = name;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        for (auto &e : exts) if (s.size() >= e.size() && s.substr(s.size()-e.size()) == e) return true;
        return false;
    }

    // Internal helper: load surface -> texture, push into pictures_, return index
    int createPictureFromSurface(SDL_Surface* surf, const std::string &path);
};
