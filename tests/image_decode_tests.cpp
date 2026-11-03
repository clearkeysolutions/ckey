#include "ckey/ckey.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

struct Pgm { size_t width, height; std::vector<uint8_t> pixels; };

static Pgm load_pgm(const std::string &path, size_t expected_side) {
    std::ifstream file(path, std::ios::binary);
    assert(file && "optical image fixture is required");
    std::string magic; size_t width=0,height=0; unsigned maxval=0;
    file >> magic >> width >> height >> maxval;
    file.get();
    assert(magic=="P5" && width==expected_side && height==expected_side && maxval==255);
    std::vector<uint8_t> pixels((std::istreambuf_iterator<char>(file)),{});
    assert(pixels.size()==width*height);
    return {width,height,std::move(pixels)};
}

int main() {
    const std::string base=std::string(CKEY_TEST_SOURCE_DIR)+"/tests/vectors/";
    auto fixture=load_pgm(base+"locator-v8-preview.pgm",670);
    auto &image=fixture.pixels; const size_t width=fixture.width, height=fixture.height;

    // The PGM is a crop of a browser-rendered v0.8 locator, not a raw generated
    // bit matrix. The symbol's 31-unit SVG canvas maps canonical [0,29]^2 to
    // [1,30]^2 within this 670 px crop.
    const double unit=static_cast<double>(width)/31.0;
    const double x0=unit, x1=30.0*unit;
    const double quad[8]={x0,x0,x1,x0,x1,x1,x0,x1};
    constexpr std::array<uint8_t,16> expected={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    std::array<uint8_t,16> decoded{}; ckey_diagnostics diag{};
    assert(ckey_calibration_score_gray8(image.data(),width,height,width,quad) > 40.0);
    assert(ckey_decode_gray8(image.data(),width,height,width,quad,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected && diag.status==CKEY_OK);

    // This crop is taken from the user's exact reader target image and resized
    // to 20 px/module. It verifies this image's marks and framing reach the core.
    auto target=load_pgm(base+"locator-v8-reader-target.pgm",580);
    const double target_edge=580.0/934.0;
    const double target_far=933.0*target_edge;
    const double target_quad[8]={target_edge,target_edge,target_far,target_edge,
                                 target_far,target_far,target_edge,target_far};
    decoded.fill(0);
    assert(ckey_decode_gray8(target.pixels.data(),target.width,target.height,target.width,
                             target_quad,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected);

    // Regression fixture cropped from the user's iPhone camera preview while
    // viewing the CKey on a laptop display. It includes screen rasterization,
    // camera blur, and slight projective skew instead of a generated bitmap.
    auto camera=load_pgm(base+"locator-v8-phone-camera.pgm",500);
    const double camera_quad[8]={24,24,462,24,462,450,24,450};
    decoded.fill(0);
    assert(ckey_decode_gray8(camera.pixels.data(),camera.width,camera.height,camera.width,
                             camera_quad,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected);

    // Far-distance phone preview: the target is only about 135 px across
    // (~4.7 px/module), but still decodes when localization supplies its field.
    auto far_camera=load_pgm(base+"locator-v8-far-camera.pgm",180);
    const double far_quad[8]={14,14,149,14,149,149,14,149};
    decoded.fill(0);
    assert(ckey_decode_gray8(far_camera.pixels.data(),far_camera.width,far_camera.height,
                             far_camera.width,far_quad,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected);

    // A rotated corner order and inverted monochrome polarity are both valid.
    const double rotated[8]={x1,x0,x1,x1,x0,x1,x0,x0};
    for (uint8_t &pixel : image) pixel=static_cast<uint8_t>(255-pixel);
    decoded.fill(0);
    assert(ckey_decode_gray8(image.data(),width,height,width,rotated,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected);

    const double invalid[8]={1,1,2,2,3,3,4,4};
    assert(ckey_decode_gray8(image.data(),width,height,width,invalid,decoded.data(),decoded.size(),&diag)==CKEY_INVALID_ARGUMENT);
    std::cout << "CKey optical image decode tests passed\n";
}
