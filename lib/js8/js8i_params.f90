parameter (NSUBMODE=8)
parameter (NCOSTAS=2)                 !Which JS8 Costas Arrays to use (1=original, 2=three symmetrical costas)

parameter (NSPS=384,  NTXDUR=4,  NDOWNSPS=12, NDD=125, JZ=250) ! 250 Hz  31.25 baud 60 wpm -18.0dB (1.0Eb/N0)  2.52s

parameter (AZ=12000.0/(1.0*NSPS)*0.64d0)!Dedupe overlap in Hz
parameter (ASTART=0.1)                  !Start delay in seconds
parameter (ASYNCMIN=1.5)                !Minimum Sync

parameter (KK=87)                     !Information bits (75 + CRC12)
parameter (ND=58)                     !Data symbols
parameter (NS=21)                     !Sync symbols (3 @ Costas 7x7)
parameter (NF=125)                    !SNR normalization
parameter (NN=NS+ND)                  !Total channel symbols (79)
parameter (NZ=NSPS*NN)                !Samples in full 15 s waveform (151,680)
parameter (NMAX=NTXDUR*12000)         !Samples in iwave (180,000)
parameter (NFFT1=2*NSPS, NH1=NFFT1/2) !Length of FFTs for symbol spectra
parameter (NSTEP=NSPS/4)              !Rough time-sync step size
parameter (NHSYM=NMAX/NSTEP-3)        !Number of symbol spectra (1/4-sym steps)
parameter (NDOWN=NSPS/NDOWNSPS)       !Downsample factor to 32 samples per symbol
parameter (NQSYMBOL=NDOWNSPS/4)       !Downsample factor of a quarter symbol
parameter (NFSRCH=5)                  !Search frequency range in Hz (i.e., +/- 2.5 Hz)
parameter (NMAXCAND=300)              !Maxiumum number of candidate signals
