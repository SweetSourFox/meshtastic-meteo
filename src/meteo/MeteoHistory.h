#pragma once

#include "MeteoTypes.h"
#include <algorithm>
#include <cmath>

namespace meteo
{

template <typename T> class RingBuffer
{
  public:
    explicit RingBuffer(int cap = 0) : cap_(cap)
    {
        if (cap_ > 0) {
            data_ = new T[cap_];
            times_ = new uint32_t[cap_];
        }
    }
    ~RingBuffer()
    {
        delete[] data_;
        delete[] times_;
    }

    void init(int cap)
    {
        delete[] data_;
        delete[] times_;
        cap_ = cap;
        count_ = 0;
        head_ = 0;
        data_ = new T[cap_];
        times_ = new uint32_t[cap_];
    }

    int count() const { return count_; }
    int capacity() const { return cap_; }

    void push(uint32_t t, T value)
    {
        if (cap_ <= 0 || !data_)
            return;
        data_[head_] = value;
        times_[head_] = t;
        head_ = (head_ + 1) % cap_;
        if (count_ < cap_)
            count_++;
    }

    void clear()
    {
        count_ = 0;
        head_ = 0;
    }

    void at(int logical, uint32_t *t, T *v) const
    {
        int idx;
        if (count_ < cap_)
            idx = logical;
        else
            idx = (head_ + logical) % cap_;
        *t = times_[idx];
        *v = data_[idx];
    }

    void newest(uint32_t *t, T *v) const
    {
        if (count_ == 0) {
            *t = 0;
            *v = T();
            return;
        }
        int idx = (head_ - 1 + cap_) % cap_;
        *t = times_[idx];
        *v = data_[idx];
    }

    void minMax(T *lo, T *hi) const
    {
        if (count_ == 0) {
            *lo = *hi = T();
            return;
        }
        T v0;
        uint32_t t0;
        at(0, &t0, &v0);
        T l = v0, h = v0;
        for (int i = 1; i < count_; i++) {
            T v;
            uint32_t t;
            at(i, &t, &v);
            if (v < l)
                l = v;
            if (v > h)
                h = v;
        }
        *lo = l;
        *hi = h;
    }

    float average() const
    {
        if (count_ == 0)
            return NAN;
        float s = 0;
        for (int i = 0; i < count_; i++) {
            T v;
            uint32_t t;
            at(i, &t, &v);
            s += float(v);
        }
        return s / float(count_);
    }

    uint32_t spanMs() const
    {
        if (count_ < 2)
            return 0;
        uint32_t t0, t1;
        T v;
        at(0, &t0, &v);
        at(count_ - 1, &t1, &v);
        return wrapMs(t1, t0);
    }

    int copyTimesValues(uint32_t *times_out, float *values_out) const
    {
        for (int i = 0; i < count_; i++) {
            T v;
            at(i, &times_out[i], &v);
            values_out[i] = float(v);
        }
        return count_;
    }

  private:
    int cap_ = 0;
    int count_ = 0;
    int head_ = 0;
    T *data_ = nullptr;
    uint32_t *times_ = nullptr;
};

class Bucket
{
  public:
    Bucket() : interval_ms_(60000) {}
    explicit Bucket(int interval_s) : interval_ms_(uint32_t(interval_s) * 1000) {}

    void reset(int interval_s = -1)
    {
        if (interval_s >= 0)
            interval_ms_ = uint32_t(interval_s) * 1000;
        sum_ = 0;
        n_ = 0;
        armed_ = false;
    }

    bool armed() const { return armed_; }
    uint32_t startMs() const { return start_ms_; }

    float add(uint32_t now_ms, float value)
    {
        if (!armed_) {
            start_ms_ = now_ms;
            armed_ = true;
            sum_ = value;
            n_ = 1;
            return NAN;
        }
        uint32_t elapsed = wrapMs(now_ms, start_ms_);
        if (elapsed < interval_ms_) {
            sum_ += value;
            n_++;
            return NAN;
        }
        float mean = sum_ / float(n_);
        start_ms_ = now_ms;
        sum_ = value;
        n_ = 1;
        return mean;
    }

  private:
    uint32_t interval_ms_;
    float sum_ = 0;
    int n_ = 0;
    uint32_t start_ms_ = 0;
    bool armed_ = false;
};

class SlpMedian
{
  public:
    float push(float value)
    {
        if (n_ == 0) {
            a_ = value;
            n_ = 1;
            return value;
        }
        if (n_ == 1) {
            b_ = value;
            n_ = 2;
            return (a_ + b_) / 2.0f;
        }
        c_ = b_;
        b_ = a_;
        a_ = value;
        n_ = 3;
        float x = a_, y = b_, z = c_;
        if (x > y)
            std::swap(x, y);
        if (y > z)
            std::swap(y, z);
        if (x > y)
            std::swap(x, y);
        return y;
    }

  private:
    float a_ = 0, b_ = 0, c_ = 0;
    int n_ = 0;
};

class MeteoHistory
{
  public:
    explicit MeteoHistory(int chart_s = 60);

    void setChartInterval(int seconds);
    void ingest(uint32_t now_ms, float temp, float rh, float press, float co2, float gas_kohm, float slp, float iaq);
    void ingestSlp(uint32_t now_ms, float slp);

    uint32_t slpCollectMs(uint32_t now_ms) const;
    void chartRange(int ch, float *lo, float *hi) const;
    void slpStats(float *lo, float *hi, float *avg, float *slope, uint32_t *span_ms) const;
    float slpDelta() const;
    float slpDelta3h() const;

    bool saveSlp(uint32_t now_unix, bool use_unix_time);
    int loadSlp(uint32_t now_unix, bool has_rtc);

    RingBuffer<float> &chart(int ch) { return charts_[ch]; }
    RingBuffer<float> &slpRing() { return slp_; }
    const RingBuffer<float> &slpRing() const { return slp_; }

  private:
    int chart_s_;
    RingBuffer<float> charts_[CHANNEL_COUNT];
    Bucket buckets_[CHANNEL_COUNT];
    RingBuffer<float> slp_;
    Bucket slp_bucket_{300};
    SlpMedian slp_med_;
};

} // namespace meteo
