import FWCore.ParameterSet.Config as cms
from Configuration.Generator.Pythia8CommonSettings_cfi import *
#from Configuration.Generator.Pythia8CUEP8M1Settings_cfi import *
from Configuration.Generator.MCTunes2017.PythiaCP5Settings_cfi import *
#source = cms.Source("EmptySource")
generator = cms.EDFilter(
    "Pythia8GeneratorFilter",
    comEnergy = cms.double(14000.0),
    crossSection = cms.untracked.double(54710000000),
    filterEfficiency = cms.untracked.double(3.0e-4),
    pythiaHepMCVerbosity = cms.untracked.bool(False),
    maxEventsToPrint = cms.untracked.int32(0),
    pythiaPylistVerbosity = cms.untracked.int32(0),
    ExternalDecays = cms.PSet(
        EvtGen130 = cms.untracked.PSet(
            operates_on_particles = cms.vint32( 0 ), # 0 (zero) means default list (hardcoded)
            # you can put here the list of particles (PDG IDs)
            # that you want decayed by EvtGen
            decay_table = cms.string('GeneratorInterface/EvtGenInterface/data/DECAY_2010.DEC'),
            particle_property_file = cms.FileInPath('GeneratorInterface/EvtGenInterface/data/evt.pdl'),
#            user_decay_file = cms.FileInPath('GeneratorInterface/ExternalDecays/data/Bs_phiphi_4K.dec'),
 user_decay_embedded= cms.vstring ('Define Hp 0.49',
'Define Hz 0.775',
'Define Hm 0.4',
'Define pHp 2.50',
'Define pHz 0.0',
'Define pHm -0.17',
'#',
'Alias      MyB_s0   B_s0',
'Alias      Myanti-B_s0   anti-B_s0',
'ChargeConj Myanti-B_s0   MyB_s0',
'Alias      MyPhi    phi',
'ChargeConj MyPhi    MyPhi',
'#',
'Decay MyB_s0',
'  1.000         MyPhi      MyPhi        PVV_CPLH 0.02 1 Hp pHp Hz pHz Hm pHm;',
'#',
'Enddecay',
'Decay Myanti-B_s0',
 ' 1.000         MyPhi      MyPhi        PVV_CPLH 0.02 1 Hp pHp Hz pHz Hm pHm;',
'Enddecay',
'#',
'Decay MyPhi',
 ' 1.000         K+          K-           VSS;',
'Enddecay',
'End'),
             list_forced_decays = cms.vstring('MyB_s0',
                                             'Myanti-B_s0'),
            ),
parameterSets = cms.vstring('EvtGen130')
        ),
    PythiaParameters = cms.PSet(
     pythia8CommonSettingsBlock,
      pythia8CP5SettingsBlock,
      #pythia8CUEP8M1SettingsBlock,
     processParameters = cms.vstring('SoftQCD:nonDiffractive = on',
                                     'PTFilter:filter = on', # this turn on the filter
                                     'PTFilter:quarkToFilter = 5', # PDG id of q quark (can be any other)
                                     'PTFilter:scaleToFilter = 1.0'),
        # This is a vector of ParameterSet names to be read, in this order
        parameterSets = cms.vstring(
            'pythia8CommonSettings',
            'pythia8CP5Settings',
            #'pythia8CUEP8M1Settings',
            'processParameters')
        )
    )
bfilter = cms.EDFilter(
    "PythiaFilter",
    MaxEta = cms.untracked.double(9999.),
    MinEta = cms.untracked.double(-9999.),
    ParticleID = cms.untracked.int32(531)
    )
phifilter = cms.EDFilter(
    "PythiaDauVFilter",
    verbose = cms.untracked.int32(0),
    NumberDaughters = cms.untracked.int32(2),
    MotherID = cms.untracked.int32(531),
    ParticleID = cms.untracked.int32(333),
    DaughterIDs = cms.untracked.vint32(321, -321),
    MinPt = cms.untracked.vdouble(1.95, 1.95),
    MinEta = cms.untracked.vdouble(-2.4, -2.4),
    MaxEta = cms.untracked.vdouble(2.4, 2.4)
    )
configurationMetadata = cms.untracked.PSet(
    version = cms.untracked.string('$Revision: 1.1 $'),
    name = cms.untracked.string
    ('$Source: /local/projects/CMSSW/rep/CMSSW/Configuration/GenProduction/python/PYTHIA6_Bs2JpsiPhi_TuneZ2_14TeV_cff.py,v $'),
    annotation = cms.untracked.string('Bs -> Jpsi Phi at 14TeV')
    )
ProductionFilterSequence = cms.Sequence(generator*bfilter*phifilter)

