#include "ckey/ckey.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main() {
    const std::string path = std::string(CKEY_TEST_SOURCE_DIR) + "/tests/vectors/locator-v8-preview.pgm";
    std::ifstream file(path, std::ios::binary);
    assert(file && "optical image fixture is required");
    std::string magic; size_t width=0,height=0; unsigned maxval=0;
    file >> magic >> width >> height >> maxval;
    file.get();
    assert(magic=="P5" && width==670 && height==670 && maxval==255);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});
    assert(image.size()==width*height);

    // The PGM is a crop of a browser-rendered v0.8 locator, not a raw generated
    // bit matrix. The symbol's 31-unit SVG canvas maps canonical [0,29]^2 to
    // [1,30]^2 within this 670 px crop.
    const double unit=static_cast<double>(width)/31.0;
    const double x0=unit, x1=30.0*unit;
    const double quad[8]={x0,x0,x1,x0,x1,x1,x0,x1};
    constexpr std::array<uint8_t,16> expected={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    std::array<uint8_t,16> decoded{}; ckey_diagnostics diag{};
    assert(ckey_decode_gray8(image.data(),width,height,width,quad,decoded.data(),decoded.size(),&diag)==CKEY_OK);
    assert(decoded==expected && diag.status==CKEY_OK);

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
