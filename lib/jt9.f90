program jt9

! Decoder for js8.  Can run stand-alone, reading data from *.wav files;
! or as the back end of js8call, with data placed in a shared memory region.

  use options
  use prog_args
  use, intrinsic :: iso_c_binding
  use FFTW3
  use timer_module, only: timer
  use timer_impl, only: init_timer, fini_timer
  use readwav

  include 'jt9com.f90'

  integer(C_INT) iret
  type(wav_header) wav
  character c
  character(len=500) optarg, infile
  character wisfile*80
!### ndepth was defined as 60001.  Why???
  integer :: arglen,stat,offset,remain,mode=0,flow=200,          &
       fhigh=4000,nrxfreq=1500,ntrperiod=1,ndepth=1,nexp_decode=0
  logical :: read_files = .true., tx9 = .false., display_help = .false., syncStats = .false.
  type (option) :: long_options(22) = [ &
    option ('help', .false., 'h', 'Display this help message', ''),          &
    option ('shmem',.true.,'s','Use shared memory for sample data','KEY'),   &
    option ('tr-period', .true., 'p', 'Tx/Rx period, default MINUTES=1',     &
        'MINUTES'),                                                          &
    option ('executable-path', .true., 'e',                                  &
        'Location of subordinate executables (KVASD) default PATH="."',      &
        'PATH'),                                                             &
    option ('data-path', .true., 'a',                                        &
        'Location of writeable data files, default PATH="."', 'PATH'),       &
    option ('temp-path', .true., 't',                                        &
        'Temporary files path, default PATH="."', 'PATH'),                   &
    option ('lowest', .true., 'L',                                           &
        'Lowest frequency decoded (JT65), default HERTZ=200', 'HERTZ'),      &
    option ('highest', .true., 'H',                                          &
        'Highest frequency decoded, default HERTZ=4007', 'HERTZ'),           &
    option ('split', .true., 'S',                                            &
        'Lowest JT9 frequency decoded, default HERTZ=2700', 'HERTZ'),        &
    option ('rx-frequency', .true., 'f',                                     &
        'Receive frequency offset, default HERTZ=1500', 'HERTZ'),            &
    option ('patience', .true., 'w',                                         &
        'FFTW3 planing patience (0-4), default PATIENCE=1', 'PATIENCE'),     &
    option ('fft-threads', .true., 'm',                                      &
        'Number of threads to process large FFTs, default THREADS=1',        &
        'THREADS'),                                                          &
    !option ('jt65', .false., '6', 'JT65 mode', ''),                          &
    !option ('jt9', .false., '9', 'JT9 mode', ''),                            &
    option ('js8', .false., '8', 'JS8 mode', ''),                            &
    option ('syncStats', .false., 'y', 'Sync only', ''),                            &
    !option ('jt4', .false., '4', 'JT4 mode', ''),                            &
    !option ('qra64', .false., 'q', 'QRA64 mode', ''),                        &
    option ('sub-mode', .true., 'b', 'Sub mode, default SUBMODE=A', 'A'),    &
    option ('depth', .true., 'd',                                            &
        'Decoding depth (1-3), default DEPTH=1', 'DEPTH'),               &
    option ('tx-jt9', .false., 'T', 'Tx mode is JT9', ''),                   &
    option ('my-call', .true., 'c', 'my callsign', 'CALL'),                  &
    option ('my-grid', .true., 'G', 'my grid locator', 'GRID'),              &
    option ('his-call', .true., 'x', 'his callsign', 'CALL'),                &
    option ('his-grid', .true., 'g', 'his grid locator', 'GRID'),            &
    option ('experience-decode', .true., 'X',                                &
        'experience based decoding flags (1..n), default FLAGS=0',           &
        'FLAGS') ]

  type(dec_data), allocatable :: shared_data
  character(len=20) :: datetime=''
  character(len=12) :: mycall='K1ABC', hiscall='W9XYZ'
  character(len=6) :: mygrid='', hisgrid='EN37'
  common/patience/npatience,nthreads
  common/decstats/ntry65a,ntry65b,n65a,n65b,num9,numfano
  data npatience/1/,nthreads/1/

  nsubmode = 0

  do
     call getopt('hs:e:a:b:r:m:p:d:f:w:t:9864qTL:S:H:c:G:x:g:X:',      &
          long_options,c,optarg,arglen,stat,offset,remain,.true.)
     if (stat .ne. 0) then
        exit
     end if
     select case (c)
        case ('h')
           display_help = .true.
        case ('s')
           read_files = .false.
           shm_key = optarg(:arglen)
        case ('e')
           exe_dir = optarg(:arglen)
        case ('a')
           data_dir = optarg(:arglen)
        case ('b')
           nsubmode = ichar (optarg(:1)) - ichar ('A')
        case ('t')
           temp_dir = optarg(:arglen)
        case ('m')
           read (optarg(:arglen), *) nthreads
        case ('p')
           read (optarg(:arglen), *) ntrperiod
        case ('d')
           read (optarg(:arglen), *) ndepth
        case ('f')
           read (optarg(:arglen), *) nrxfreq
        case ('L')
           read (optarg(:arglen), *) flow
        case ('H')
           read (optarg(:arglen), *) fhigh
        !case ('q')
        !   mode = 164
        !case ('4')
        !   mode = 4
        !case ('6')
        !   if (mode.lt.65) mode = mode + 65
        !case ('9')
        !   if (mode.lt.9.or.mode.eq.65) mode = mode + 9
        case ('8')
           mode = 8
        case ('y')
           syncStats = .true.
        case ('T')
           tx9 = .true.
        case ('w')
           read (optarg(:arglen), *) npatience
        case ('c')
           read (optarg(:arglen), *) mycall
        case ('G')
           read (optarg(:arglen), *) mygrid
        case ('x')
           read (optarg(:arglen), *) hiscall
        case ('g')
           read (optarg(:arglen), *) hisgrid
        case ('X')
           read (optarg(:arglen), *) nexp_decode
     end select
  end do

  if (display_help .or. stat .lt. 0                      &
       .or. (.not. read_files .and. remain .gt. 0)       &
       .or. (read_files .and. remain .lt. 1)) then

     print *, 'Usage: js8 [OPTIONS] file1 [file2 ...]'
     print *, '       Reads data from *.wav files.'
     print *, ''
     print *, '       js8 -s <key> [-w patience] [-m threads] [-e path] [-a path] [-t path]'
     print *, '       Gets data from shared memory region with key==<key>'
     print *, ''
     print *, 'OPTIONS:'
     print *, ''
     do i = 1, size (long_options)
       call long_options(i) % print (6)
     end do
     go to 999
  endif
  
  iret=fftwf_init_threads()            !Initialize FFTW threading 

! Default to 1 thread, but use nthreads for the big ones
  call fftwf_plan_with_nthreads(1)

! Import FFTW wisdom, if available
  wisfile=trim(data_dir)//'/jt9_wisdom.dat'// C_NULL_CHAR
  iret=fftwf_import_wisdom_from_filename(wisfile)

  ntry65a=0
  ntry65b=0
  n65a=0
  n65b=0
  num9=0
  numfano=0

  if (.not. read_files) then
     call jt9a()          !We're running under control of WSJT-X
     go to 999
  endif

  allocate(shared_data)
  nflatten=0

  do iarg = offset + 1, offset + remain
     call get_command_argument (iarg, optarg, arglen)
     infile = optarg(:arglen)
     call wav%read (infile)
     nfsample=wav%audio_format%sample_rate
     i1=index(infile,'.wav')
     if(i1.lt.1) i1=index(infile,'.WAV')
     if(infile(i1-5:i1-5).eq.'_') then
        read(infile(i1-4:i1-1),*,err=1) nutc
     else
        read(infile(i1-6:i1-1),*,err=1) nutc
     endif
     go to 2
1    nutc=0
2    nsps=0
     if(ntrperiod.eq.1)  then
        nsps=6912
        shared_data%params%nzhsym=181
     else if(ntrperiod.eq.2)  then
        nsps=15360
        shared_data%params%nzhsym=178
     else if(ntrperiod.eq.5)  then
        nsps=40960
        shared_data%params%nzhsym=172
     else if(ntrperiod.eq.10) then
        nsps=82944
        shared_data%params%nzhsym=171
     else if(ntrperiod.eq.30) then
        nsps=252000
        shared_data%params%nzhsym=167
     endif
     if(nsps.eq.0) stop 'Error: bad TRperiod'

     kstep=nsps/2
     k=0
     nhsym0=-999
     npts=(60*ntrperiod-6)*12000
     if(iarg .eq. offset + 1) then
        call init_timer (trim(data_dir)//'/timer.out')
        call timer('jt9     ',0)
     endif

     shared_data%id2=0          !??? Why is this necessary ???

     do iblk=1,npts/kstep
        k=iblk*kstep
        call timer('read_wav',0)
        read(unit=wav%lun,end=3) shared_data%id2(k-kstep+1:k)
        go to 4
3       call timer('read_wav',1)
        print*,'EOF on input file ',infile
        exit
4       call timer('read_wav',1)
        nhsym=(k-2048)/kstep
        if(nhsym.ge.1 .and. nhsym.ne.nhsym0) then
           nhsym0=nhsym
           if(nhsym.ge.181) exit
        endif
     enddo
     close(unit=wav%lun)
     shared_data%params%nutc=nutc
     shared_data%params%ndiskdat=.true.
     shared_data%params%ntr=60
     shared_data%params%nfqso=nrxfreq
     shared_data%params%newdat=.true.
     shared_data%params%npts8=74736
     shared_data%params%nfa=flow
     shared_data%params%nfb=fhigh
     shared_data%params%ntol=20
     shared_data%params%kin=64800
     shared_data%params%nzhsym=181
     shared_data%params%ndepth=ndepth
     shared_data%params%lft8apon=.true.
     shared_data%params%ljt65apon=.true.
     shared_data%params%napwid=75
     shared_data%params%dttol=3.
     shared_data%params%syncStats=syncStats

!     shared_data%params%minsync=0       !### TEST ONLY
!     shared_data%params%nfqso=1500     !### TEST ONLY
!     mycall="G3WDG       "              !### TEST ONLY
!     hiscall="VK7MO       "             !### TEST ONLY
!     hisgrid="QE37        "             !### TEST ONLY
     if(mode.eq.164 .and. nsubmode.lt.100) nsubmode=nsubmode+100

     shared_data%params%naggressive=0
     shared_data%params%n2pass=2
!     shared_data%params%nranera=8                      !### ntrials=10000
     shared_data%params%nranera=6                      !### ntrials=3000
     shared_data%params%nrobust=.false.
     shared_data%params%nexp_decode=nexp_decode
     shared_data%params%mycall=transfer(mycall,shared_data%params%mycall)
     shared_data%params%mygrid=transfer(mygrid,shared_data%params%mygrid)
     shared_data%params%hiscall=transfer(hiscall,shared_data%params%hiscall)
     shared_data%params%hisgrid=transfer(hisgrid,shared_data%params%hisgrid)
     if (tx9) then
        shared_data%params%ntxmode=9
     else
        shared_data%params%ntxmode=65
     end if
     if (mode.eq.0) then
        shared_data%params%nmode=65+9
     else
        shared_data%params%nmode=mode
     end if
     shared_data%params%nsubmode=nsubmode
     datetime="2013-Apr-16 15:13" !### Temp
     shared_data%params%datetime=transfer(datetime,shared_data%params%datetime)
     shared_data%params%kposA=0
     shared_data%params%kposB=0
     shared_data%params%kposC=0
     shared_data%params%kposE=0
     shared_data%params%kposI=0
     shared_data%params%kszA=NMAX-1
     shared_data%params%kszB=NMAX-1
     shared_data%params%kszC=NMAX-1
     shared_data%params%kszE=NMAX-1
     shared_data%params%kszI=NMAX-1
     call multimode_decoder(shared_data%id2,shared_data%params)
  enddo

  call timer('jt9     ',1)
  call timer('jt9     ',101)

999 continue
! Output decoder statistics
  call fini_timer ()
!  open (unit=12, file=trim(data_dir)//'/timer.out', status='unknown', position='append')
!  write(12,1100) n65a,ntry65a,n65b,ntry65b,numfano,num9
!1100 format(58('-')/'   JT65_1  Tries_1  JT65_2 Tries_2    JT9   Tries'/  &
!            58('-')/6i8)

! Save wisdom and free memory
  iret=fftwf_export_wisdom_to_filename(wisfile)
  call four2a(a,-1,1,1,1)
  call filbig(a,-1,1,0.0,0,0,0,0,0)        !used for FFT plans
  call fftwf_cleanup_threads()
  call fftwf_cleanup()
end program jt9
