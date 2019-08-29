import FWCore.ParameterSet.Config as cms
from Configuration.Generator.Pythia8CommonSettings_cfi import *
from Configuration.Generator.MCTunes2017.PythiaCP5Settings_cfi import *
from GeneratorInterface.EvtGenInterface.EvtGenSetting_cff import *


generator = cms.EDFilter("Pythia8GeneratorFilter",
    pythiaPylistVerbosity = cms.untracked.int32(0),
    pythiaHepMCVerbosity = cms.untracked.bool(False),
    maxEventsToPrint = cms.untracked.int32(0),
    comEnergy = cms.double(14000.0),
    ExternalDecays = cms.PSet(
        EvtGen130 = cms.untracked.PSet(
            decay_table = cms.string('GeneratorInterface/EvtGenInterface/data/DECAY_2014_NOLONGLIFE.DEC'),
            particle_property_file = cms.FileInPath('GeneratorInterface/EvtGenInterface/data/evt_2014.pdl'),
            list_forced_decays = cms.vstring('MyB0','Myanti-B0'),        
            operates_on_particles = cms.vint32(),    # we care just about our signal particles
            convertPythiaCodes = cms.untracked.bool(False),
            #user_decay_file = cms.vstring('GeneratorInterface/ExternalDecays/data/Bd_KstarJpsi_mm.dec')
	    user_decay_embedded= cms.vstring(
"""
#
# This is the decay file for the decay B0 -> J/PSI(->mumu) K*(->Kpi)
#
Alias      MyB0   B0
Alias      Myanti-B0   anti-B0
ChargeConj Myanti-B0   MyB0 
Alias      MyJpsi      J/psi
ChargeConj MyJpsi      MyJpsi
Alias      MyK*0       K*0
Alias      MyK*0bar    anti-K*0
ChargeConj MyK*0       MyK*0bar
#
Decay MyB0
  1.000    MyJpsi      MyK*0             SVV_HELAMP 0.159 1.563 0.775 0.0 0.612 2.712;
Enddecay
CDecay Myanti-B0
#
Decay MyJpsi
  1.000         mu+       mu-            PHOTOS VLL;
Enddecay
#
Decay MyK*0
  1.000        K+        pi-                    VSS;
Enddecay
Decay MyK*0bar
  1.000        K-        pi+                    VSS;
Enddecay 
End
"""
	    ),
        ),
        parameterSets = cms.vstring('EvtGen130')
    ),
    PythiaParameters = cms.PSet(
        pythia8CommonSettingsBlock,
        pythia8CP5SettingsBlock,
        processParameters = cms.vstring('SoftQCD:nonDiffractive = on',
                                        'PTFilter:filter = on', 
            							'PTFilter:quarkToFilter = 5', 
            							'PTFilter:scaleToFilter = 1.0'
            ),
        parameterSets = cms.vstring('pythia8CommonSettings',
                                    'pythia8CP5Settings',
                                    'processParameters',
                                    )
    )
)

###### Filters ##########
bfilter = cms.EDFilter(
    "PythiaFilter",
    MaxEta = cms.untracked.double(9999.),
    MinEta = cms.untracked.double(-9999.),
    ParticleID = cms.untracked.int32(511) ## Bd
    )

decayfilter = cms.EDFilter(
    "PythiaDauVFilter",
    verbose         = cms.untracked.int32(1),
    NumberDaughters = cms.untracked.int32(2),
    ParticleID      = cms.untracked.int32(511),  ## Bd
    DaughterIDs     = cms.untracked.vint32(443, 313), ## J/psi and K*
    MinPt           = cms.untracked.vdouble(-1., -1.),
    MinEta          = cms.untracked.vdouble(-9999., -9999.),
    MaxEta          = cms.untracked.vdouble( 9999.,  9999.)
    )

kstarfilter = cms.EDFilter("PythiaDauVFilter",
    DaughterIDs = cms.untracked.vint32(321, -211),
    MaxEta = cms.untracked.vdouble(9999.0, 9999.0),
    MinEta = cms.untracked.vdouble(-9999.0, -9999.0),
    MinPt = cms.untracked.vdouble(-1.,-1.),
    MotherID = cms.untracked.int32(511),
    NumberDaughters = cms.untracked.int32(2),
    ParticleID = cms.untracked.int32(313),
    verbose = cms.untracked.int32(1)
)

jpsifilter = cms.EDFilter(
    "PythiaDauVFilter",
    verbose         = cms.untracked.int32(1), 
    NumberDaughters = cms.untracked.int32(2), 
    MotherID        = cms.untracked.int32(511),  
    ParticleID      = cms.untracked.int32(443),  
    DaughterIDs     = cms.untracked.vint32(13, -13),
    MinPt           = cms.untracked.vdouble(-1., -1.), 
    MinEta          = cms.untracked.vdouble(-9999., -9999.), 
    MaxEta          = cms.untracked.vdouble(9999., 9999.)
    )

mu3filter = cms.EDFilter("MCMultiParticleFilter",
            src = cms.untracked.InputTag("generator", "unsmeared"),
            Status = cms.vint32(1),
            ParticleID = cms.vint32(13),
            PtMin = cms.vdouble(0.),
            NumRequired = cms.int32(3),
            EtaMax = cms.vdouble(999.),
            AcceptMore = cms.bool(True)
            )

#mufilter = cms.EDFilter("PythiaFilter",  # bachelor muon with kinematic cuts.
#    MaxEta = cms.untracked.double(2.5),
#    MinEta = cms.untracked.double(-2.5),
#    MinPt = cms.untracked.double(5.),
#    ParticleID = cms.untracked.int32(13),
#)

## Probe filter neglets ParticleID coming from the topology
## of such GrandMomoID, MomID, SisterIDs and AuntIDs with respect
## the NumberOfSisters, NumberOfAunts and the kinematical cuts.
probefilter=cms.EDFilter("PythiaProbeFilter",  # bachelor muon with kinematic cuts.
    MaxEta = cms.untracked.double(2.5),
    MinEta = cms.untracked.double(-2.5),
    MinPt = cms.untracked.double(5.),
    ParticleID = cms.untracked.int32(13),
    MomID=cms.untracked.int32(443),
    GrandMomID = cms.untracked.int32(511),
    NumberOfSisters= cms.untracked.int32(1),
    NumberOfAunts= cms.untracked.int32(1),
    SisterIDs=cms.untracked.vint32(-13),
    AuntIDs=cms.untracked.vint32(321),
)


ProductionFilterSequence = cms.Sequence(generator*bfilter*decayfilter*kstarfilter*jpsifilter*mu3filter*probefilter)

