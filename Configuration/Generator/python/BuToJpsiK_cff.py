import FWCore.ParameterSet.Config as cms
from Configuration.Generator.Pythia8CommonSettings_cfi import *
from Configuration.Generator.MCTunes2017.PythiaCP5Settings_cfi import *
from GeneratorInterface.EvtGenInterface.EvtGenSetting_cff import *
#from Configuration.Generator.PSweightsPythia.PythiaPSweightsSettings_cfi import *

generator = cms.EDFilter("Pythia8GeneratorFilter",
                         pythiaPylistVerbosity = cms.untracked.int32(0),
                         pythiaHepMCVerbosity = cms.untracked.bool(False),
                         comEnergy = cms.double(14000.0),
                         maxEventsToPrint = cms.untracked.int32(0),
                         ExternalDecays = cms.PSet(
        EvtGen130 = cms.untracked.PSet(
            decay_table = cms.string('GeneratorInterface/EvtGenInterface/data/DECAY_2010.DEC'),
            particle_property_file = cms.FileInPath('GeneratorInterface/EvtGenInterface/data/evt.pdl'),
            user_decay_file = cms.vstring('GeneratorInterface/ExternalDecays/data/Bu_JpsiK.dec'),
            list_forced_decays = cms.vstring('MyB+',
                                              'MyB-'),
            operates_on_particles = cms.vint32()
            ),
        parameterSets = cms.vstring('EvtGen130')
        ),
                         PythiaParameters = cms.PSet(
        pythia8CommonSettingsBlock,
        pythia8CP5SettingsBlock,
        #pythia8PSweightsSettingsBlock,
        processParameters = cms.vstring(
            'HardQCD:all = on',
            'PhaseSpace:pTHatMin = 8.',            
            ),
        parameterSets = cms.vstring('pythia8CommonSettings',
                                    'pythia8CP5Settings',
                                    #'pythia8PSweightsSettings',
                                    'processParameters',
                                    )
        )
                         )

generator.PythiaParameters.processParameters.extend(EvtGenExtraParticles)

#configurationMetadata = cms.untracked.PSet(
#    version = cms.untracked.string('$Revision: 1.1 $'),
#    name = cms.untracked.string('$Source: Configuration/Generator/python/BuToKstarMuMu_forSTEAM_13TeV_TuneCUETP8M1_cfi.py $'),
#    annotation = cms.untracked.string('Summer14: Pythia8+EvtGen130 generation of Bu --> K* Mu+Mu-, 13TeV, Tune CUETP8M1')
#    )

###########
# Filters #
###########
# Filter only pp events which produce a B+:
bufilter = cms.EDFilter("PythiaFilter", ParticleID = cms.untracked.int32(521))



jpsifilter = cms.EDFilter(
        "PythiaDauVFilter",
        verbose         = cms.untracked.int32(0), 
        NumberDaughters = cms.untracked.int32(2), 
        MotherID        = cms.untracked.int32(521),  
        ParticleID      = cms.untracked.int32(443),  
        DaughterIDs     = cms.untracked.vint32(13, -13),
        MinPt           = cms.untracked.vdouble(2.8, 2.8), 
        MinEta          = cms.untracked.vdouble(-2.4, -2.4), 
        MaxEta          = cms.untracked.vdouble( 2.4,  2.4)
        )


ProductionFilterSequence = cms.Sequence(generator*bufilter*jpsifilter)
