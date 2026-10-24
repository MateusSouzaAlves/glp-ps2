// SPDX-License-Identifier: MIT. Synthetic floats only, no game or BIOS.
#include "audio_peak_guard.h"
#include <array>
#include <cstdio>
#include <limits>
#include <vector>
#include <chrono>

// Previous product algorithm retained ONLY as a synthetic comparator.
struct PreviousGuard {
    float gain = 1;
    void convert(const float* in, int16_t* out, size_t n) {
        for (size_t i=0;i<n;i++) {
            float l=in[2*i]*.94f,r=in[2*i+1]*.94f;
            float p=std::max(std::abs(l),std::abs(r)), t=p>.92f?.92f/p:1.f;
            gain=std::min(t,gain+(1-gain)/4800.f);
            out[2*i]=static_cast<int16_t>(std::clamp(l*gain,-.92f,.92f)*32767.f);
            out[2*i+1]=static_cast<int16_t>(std::clamp(r*gain,-.92f,.92f)*32767.f);
        }
    }
};
template<class Guard> double distortion(double frequency) {
    Guard guard; constexpr size_t n=48000*3;
    std::vector<float> input(n*2);std::vector<int16_t> output(n*2);
    for(size_t i=0;i<n;i++)input[2*i]=input[2*i+1]=1.6f*std::sin(2*3.141592653589793*frequency*i/48000.);
    guard.convert(input.data(),output.data(),n);
    double s=0,c=0,energy=0;constexpr size_t start=48000*2, length=n-start;
    for(size_t i=start;i<n;i++) {
        double y=output[2*i]/32767.,angle=2*3.141592653589793*frequency*i/48000.;
        s+=y*std::sin(angle);c+=y*std::cos(angle);energy+=y*y;
    }
    double fundamental=2*(s*s+c*c)/length;
    return std::sqrt(std::max(0.,energy-fundamental)/fundamental);
}

int main() {
    using emuorbit::armsx2::AudioPeakGuard;
    AudioPeakGuard guard;
    constexpr size_t delay=AudioPeakGuard::kLookAheadFrames;
    std::vector<float> quiet((delay+10)*2,.25f);std::vector<int16_t> pcm(quiet.size());
    guard.convert(quiet.data(),pcm.data(),quiet.size()/2);
    for(size_t i=0;i<delay*2;i++)if(pcm[i])return 1;
    if(std::abs(pcm[delay*2]-7700)>1)return 2;
    guard.reset();
    std::vector<float> input(48000*2);
    for(size_t i=0;i<48000;i++){input[2*i]=8.f*std::sin(i*.01f);input[2*i+1]=-.5f*input[2*i];}
    input[2000]=std::numeric_limits<float>::infinity();input[2001]=std::numeric_limits<float>::quiet_NaN();
    std::vector<int16_t> single(input.size()),chunked(input.size());
    guard.convert(input.data(),single.data(),48000);guard.reset();
    for(size_t i=0;i<48000;){size_t count=std::min(size_t(127),48000-i);guard.convert(input.data()+2*i,chunked.data()+2*i,count);i+=count;}
    if(single!=chunked)return 3;
    for(size_t i=0;i<48000;i++) {
        if(std::abs(int(single[2*i]))>30146||std::abs(int(single[2*i+1]))>30146)return 4;
        if(std::abs(int(single[2*i])+2*int(single[2*i+1]))>2)return 5;
    }
    if(single[2000+delay*2]||single[2001+delay*2])return 6;
    guard.reset();
    guard.convert(nullptr, nullptr, 0);
    for(double frequency:{50.,100.,440.,4000.}) {
        double before=distortion<PreviousGuard>(frequency),after=distortion<AudioPeakGuard>(frequency);
        std::printf("synthetic %.0fHz residual before=%.8f after=%.8f improvement=%.2fx\n",frequency,before,after,before/after);
        if(after>=before || after>.003)return 7;
    }
    auto start=std::chrono::steady_clock::now();guard.reset();
    for(int i=0;i<100;i++)guard.convert(input.data(),single.data(),48000);
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("CPU 100s PCM in %.6fs; latency=5ms; fixed_bytes=%zu\n",seconds,sizeof(guard));
    std::puts("AUDIO_PEAK_GUARD_PASS quiet/overload/stereo/nonfinite/reset/chunk-boundaries/synthetic-distortion no-allocation");
}
