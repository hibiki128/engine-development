#include "Instrument.h"
#include "DrumMachineInstrument.h"
#include "SamplerInstrument.h"
#include "SynthInstrument.h"

namespace Hagine {

std::unique_ptr<Instrument> Instrument::Create(InstrumentKind kind)
{
    switch (kind)
    {
    case InstrumentKind::DrumMachine:
        return std::make_unique<DrumMachineInstrument>();
    case InstrumentKind::Sampler:
        return std::make_unique<SamplerInstrument>();
    case InstrumentKind::Synth:
    default:
        return std::make_unique<SynthInstrument>();
    }
}

} // namespace Hagine
