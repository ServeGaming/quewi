#pragma once
#include "audio/AudioEffect.h"

#include <algorithm>
#include <vector>

namespace quewi::audio {

// Base for effects whose parameters are a plain table of ranged floats (all
// the slider-card effects: Distortion, Lo-Fi, Pitch Shift, Tremolo). The
// subclass declares its table once and reads values by index in process();
// values are plain floats written from the UI thread, as in the other
// effects.
class SimpleEffect : public AudioEffect {
    Q_OBJECT
public:
    using AudioEffect::AudioEffect;

    QStringList parameterIds() const override {
        QStringList ids;
        for (const auto &p : m_params) ids << p.id;
        return ids;
    }
    QString parameterLabel(const QString &id) const override {
        const int i = indexOf(id);
        return i >= 0 ? m_params[size_t(i)].label : id;
    }
    float parameterValue(const QString &id) const override {
        const int i = indexOf(id);
        return i >= 0 ? m_params[size_t(i)].value : 0.f;
    }
    void setParameterValue(const QString &id, float v) override {
        const int i = indexOf(id);
        if (i < 0) return;
        auto &p = m_params[size_t(i)];
        p.value = std::clamp(v, p.min, p.max);
        paramChanged(i);
        emit parameterChanged(id, p.value);
    }
    QPair<float,float> parameterRange(const QString &id) const override {
        const int i = indexOf(id);
        return i >= 0 ? qMakePair(m_params[size_t(i)].min, m_params[size_t(i)].max)
                      : qMakePair(0.f, 1.f);
    }
    float parameterDefault(const QString &id) const override {
        const int i = indexOf(id);
        return i >= 0 ? m_params[size_t(i)].def : 0.f;
    }
    int parameterDecimals(const QString &id) const override {
        const int i = indexOf(id);
        return i >= 0 ? m_params[size_t(i)].decimals : 1;
    }

protected:
    struct Param {
        QString id, label;
        float min, max, def;
        int decimals;
        float value;
    };
    // Called by the subclass constructor, in display order.
    void addParam(const QString &id, const QString &label,
                  float min, float max, float def, int decimals = 2) {
        m_params.push_back({id, label, min, max, def, decimals, def});
    }
    float p(int i) const { return m_params[size_t(i)].value; }
    // Hook for recomputing derived coefficients when a parameter moves.
    virtual void paramChanged(int) {}

private:
    int indexOf(const QString &id) const {
        for (size_t i = 0; i < m_params.size(); ++i)
            if (m_params[i].id == id) return int(i);
        return -1;
    }
    std::vector<Param> m_params;
};

} // namespace quewi::audio
