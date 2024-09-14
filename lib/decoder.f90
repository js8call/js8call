subroutine multimode_decoder(id2,params)

  !$ use omp_lib
  use prog_args
  use timer_module, only: timer
  use js8a_decode
  use js8b_decode
  use js8c_decode
  use js8e_decode
  use js8i_decode

  include 'jt9com.f90'
  include 'timer_common.inc'

  type, extends(js8a_decoder) :: counting_js8a_decoder
     integer :: decoded
  end type counting_js8a_decoder

  type, extends(js8b_decoder) :: counting_js8b_decoder
     integer :: decoded
  end type counting_js8b_decoder

  type, extends(js8c_decoder) :: counting_js8c_decoder
     integer :: decoded
  end type counting_js8c_decoder

  type, extends(js8e_decoder) :: counting_js8e_decoder
     integer :: decoded
  end type counting_js8e_decoder

  type, extends(js8i_decoder) :: counting_js8i_decoder
     integer :: decoded
  end type counting_js8i_decoder

  logical single_decode,bVHF,newdat
  integer pos, sz, firstsize, secondsize
  integer*2 id0(NTMAX*12000)
  integer*2 id2(NTMAX*12000)
  type(params_block) :: params
  character(len=20) :: datetime
  character(len=12) :: mycall, hiscall
  save
  type(counting_js8a_decoder)  :: my_js8a
  type(counting_js8b_decoder) :: my_js8b
  type(counting_js8c_decoder) :: my_js8c
  type(counting_js8e_decoder) :: my_js8e
  type(counting_js8i_decoder) :: my_js8i

  !cast C character arrays to Fortran character strings
  datetime=transfer(params%datetime, datetime)
  mycall=transfer(params%mycall,mycall)
  hiscall=transfer(params%hiscall,hiscall)

  ! initialize decode counts
  my_js8a%decoded = 0
  my_js8b%decoded = 0
  my_js8c%decoded = 0
  my_js8e%decoded = 0
  my_js8i%decoded = 0

  single_decode=iand(params%nexp_decode,32).ne.0
  bVHF=iand(params%nexp_decode,64).ne.0
  if(mod(params%nranera,2).eq.0) ntrials=10**(params%nranera/2)
  if(mod(params%nranera,2).eq.1) ntrials=3*10**(params%nranera/2)
  if(params%nranera.eq.0) ntrials=0
  
  nfail=0
  if(params%nmode.eq.8) then
    n30z=0
    nwrap=0
    nfox=0
  endif

  write(*,1012) params%nsubmode, params%nsubmodes
1012 format('<DecodeStarted>',2i4)

  if(params%nmode.eq.8 .and. (params%nsubmode.eq.8 .or. iand(params%nsubmodes, 16).eq.16)) then
! We're in JS8 mode I
     call timer('decjs8i ',0)
     newdat=params%newdat
     write(*,*) '<DecodeDebug> mode I decode started'

     ! copy the relevant frames for decoding
     pos = max(0,params%kposI)
     sz = max(0,params%kszI)
     id0=0
     imax=int(NTMAX*12000)

     if(params%syncStats) then
        write(*,*) '<DecodeSyncMeta> sync start', pos, sz
     endif

     if((imax-pos).lt.sz) then
       ! this means that the first part of the id0 is at the end of the buffer
       ! and the second half is at the beginning of the buffer
       firstsize=int(imax-pos)-1
       secondsize=int(sz-firstsize)+1
       id0(1:firstsize+1)=id2(pos+1:pos+firstsize+1)
       id0(firstsize+1:firstsize+secondsize+1)=id2(1:secondsize+1)
     else
       id0(1:sz+1)=id2(pos+1:pos+sz+1)
     endif
     
     call my_js8i%decode(js8i_decoded,id0,params%nQSOProgress,params%nfqso,   &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,params%ndepth, &
          logical(params%nagain),logical(params%lft8apon),params%napwid,      &
          logical(params%syncStats))

     write(*,*) '<DecodeDebug> mode I decode finished'
     
     call timer('decjs8i ',1)
  endif

  if(params%nmode.eq.8 .and. (params%nsubmode.eq.4 .or. iand(params%nsubmodes, 8).eq.8)) then
! We're in JS8 mode E
     call timer('decjs8e ',0)
     newdat=params%newdat
     write(*,*) '<DecodeDebug> mode E decode started'

     ! copy the relevant frames for decoding
     pos = max(0,params%kposE)
     sz = max(0,params%kszE)
     id0=0
     imax=int(NTMAX*12000)

     if(params%syncStats) then
        write(*,*) '<DecodeSyncMeta> sync start', pos, sz
     endif

     if((imax-pos).lt.sz) then
       ! this means that the first part of the id0 is at the end of the buffer
       ! and the second half is at the beginning of the buffer
       firstsize=int(imax-pos)-1
       secondsize=int(sz-firstsize)+1
       id0(1:firstsize+1)=id2(pos+1:pos+firstsize+1)
       id0(firstsize+1:firstsize+secondsize+1)=id2(1:secondsize+1)
     else
       id0(1:sz+1)=id2(pos+1:pos+sz+1)
     endif
     
     call my_js8e%decode(js8e_decoded,id0,params%nQSOProgress,params%nfqso,   &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,params%ndepth, &
          logical(params%nagain),logical(params%lft8apon),params%napwid,      &
          logical(params%syncStats))

     write(*,*) '<DecodeDebug> mode E decode finished'
     
     call timer('decjs8e ',1)
  endif

  if(params%nmode.eq.8 .and. (params%nsubmode.eq.2 .or. iand(params%nsubmodes, 4).eq.4)) then
! We're in JS8 mode C
     call timer('decjs8c ',0)
     newdat=params%newdat
     write(*,*) '<DecodeDebug> mode C decode started'

     ! copy the relevant frames for decoding
     pos = max(0,params%kposC)
     sz = max(0,params%kszC)
     id0=0
     imax=int(NTMAX*12000)

     if(params%syncStats) then
        write(*,*) '<DecodeSyncMeta> sync start', pos, sz
     endif

     if((imax-pos).lt.sz) then
       ! this means that the first part of the id0 is at the end of the buffer
       ! and the second half is at the beginning of the buffer
       firstsize=int(imax-pos)-1
       secondsize=int(sz-firstsize)+1
       id0(1:firstsize+1)=id2(pos+1:pos+firstsize+1)
       id0(firstsize+1:firstsize+secondsize+1)=id2(1:secondsize+1)
     else
       id0(1:sz+1)=id2(pos+1:pos+sz+1)
     endif
     
     call my_js8c%decode(js8c_decoded,id0,params%nQSOProgress,params%nfqso,   &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,params%ndepth, &
          logical(params%nagain),logical(params%lft8apon),params%napwid,      &
          logical(params%syncStats))

     write(*,*) '<DecodeDebug> mode C decode finished'
     
     call timer('decjs8c ',1)
  endif

  if(params%nmode.eq.8 .and. (params%nsubmode.eq.1 .or. iand(params%nsubmodes, 2).eq.2)) then
! We're in JS8 mode B
     call timer('decjs8b ',0)
     newdat=params%newdat
     write(*,*) '<DecodeDebug> mode B decode started'

     ! copy the relevant frames for decoding
     pos = max(0,params%kposB)
     sz = max(0,params%kszB)
     id0=0
     imax=int(NTMAX*12000)

     if(params%syncStats) then
        write(*,*) '<DecodeSyncMeta> sync start', pos, sz
     endif

     if((imax-pos).lt.sz) then
       ! this means that the first part of the id0 is at the end of the buffer
       ! and the second half is at the beginning of the buffer
       firstsize=int(imax-pos)-1
       secondsize=int(sz-firstsize)+1
       id0(1:firstsize+1)=id2(pos+1:pos+firstsize+1)
       id0(firstsize+1:firstsize+secondsize+1)=id2(1:secondsize+1)
     else
       id0(1:sz+1)=id2(pos+1:pos+sz+1)
     endif
     
     call my_js8b%decode(js8b_decoded,id0,params%nQSOProgress,params%nfqso,   &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,params%ndepth, &
          logical(params%nagain),logical(params%lft8apon),params%napwid,      &
          logical(params%syncStats))

     write(*,*) '<DecodeDebug> mode B decode finished'
     
     call timer('decjs8b ',1)
  endif

  if(params%nmode.eq.8 .and. (params%nsubmode.eq.0 .or. iand(params%nsubmodes, 1).eq.1)) then
! We're in JS8 mode A
     call timer('decjs8a ',0)
     newdat=params%newdat
     write(*,*) '<DecodeDebug> mode A decode started'

     ! copy the relevant frames for decoding
     pos = int(max(0,params%kposA))
     sz = int(max(0,params%kszA))
     id0=0
     imax=int(NTMAX*12000)

     if(params%syncStats) then
        write(*,*) '<DecodeSyncMeta> sync start', pos, sz
     endif

     if((imax-pos).lt.sz) then
       ! this means that the first part of the id0 is at the end of the buffer
       ! and the second half is at the beginning of the buffer
       firstsize=int(imax-pos)-1
       secondsize=int(sz-firstsize)+1
       id0(1:firstsize+1)=id2(pos+1:pos+firstsize+1)
       id0(firstsize+1:firstsize+secondsize+1)=id2(1:secondsize+1)
     else
       id0(1:sz+1)=id2(pos+1:pos+sz+1)
     endif
     
     call my_js8a%decode(js8a_decoded,id0,params%nQSOProgress,params%nfqso,   &
          params%nftx,newdat,params%nutc,params%nfa,params%nfb,params%ndepth, &
          logical(params%nagain),logical(params%lft8apon),params%napwid,      &
          logical(params%syncStats))

     write(*,*) '<DecodeDebug> mode A decode finished'

     call timer('decjs8a ',1)
  endif

  write(*,*) '<DecodeDebug> finished'
  call flush(6)

  ndecoded = my_js8a%decoded + my_js8b%decoded + my_js8c%decoded + my_js8e%decoded + my_js8i%decoded
  !call sleep_msec(3000)
  write(*,1010) ndecoded
1010 format('<DecodeFinished>',i4)
  call flush(6)
  return

contains

  subroutine js8_decoded (snr,dt,freq,decoded,nap,qual,submode)
    implicit none

    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    character c1*12,c2*12,g2*4,w*4
    integer i0,i1,i2,i3,i4,i5,n30,nwrap,n
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer, intent(in) :: submode
    character*3 m
    character*2 annot
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    data first/.true./
    save

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    if(first) then
       n30z=0
       nwrap=0
       nfox=0
       first=.false.
    endif
    
    decoded0=decoded

    annot='  ' 
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(22:22)='?'
    endif


    m = ' ~ '
    if(submode.eq.0) m=' A '
    if(submode.eq.1) m=' B '
    if(submode.eq.2) m=' C '
    if(submode.eq.4) m=' E '
    if(submode.eq.8) m=' I '


    i0=index(decoded0,';')
    if(i0.le.0) write(*,1000) params%nutc,snr,dt,nint(freq),m,decoded0(1:22),annot
1000 format(i6.6,i4,f5.1,i5,a3,1x,a22,1x,a2)
    if(i0.gt.0) write(*,1001) params%nutc,snr,dt,nint(freq),m,decoded0
1001 format(i6.6,i4,f5.1,i5,a3,1x,a37)

    i1=index(decoded0,' ')
    i2=i1 + index(decoded0(i1+1:),' ')
    i3=i2 + index(decoded0(i2+1:),' ')
    if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
       c1=decoded0(1:i1-1)//'            '
       c2=decoded0(i1+1:i2-1)
       g2=decoded0(i2+1:i3-1)
       b0=c1.eq.mycall
       if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
       if(len(trim(c1)).ne.len(trim(mycall))) then
          i4=index(trim(c1),trim(mycall))
          i5=index(trim(mycall),trim(c1))
          if(i4.ge.1 .or. i5.ge.1) b0=.true.
       endif
       b1=i3-i2.eq.5 .and. isgrid4(g2)
       b2=i3-i2.eq.1
       if(b0 .and. (b1.or.b2) .and. nint(freq).ge.1000) then
          n=params%nutc
          n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
          if(n30.lt.n30z) nwrap=nwrap+5760    !New UTC day, handle the wrap
          n30z=n30
          n30=n30+nwrap
          nfox=nfox+1
       endif
    endif
    
    call flush(6)

    return
  end subroutine js8_decoded

  subroutine js8a_decoded (this,snr,dt,freq,decoded,nap,qual)
    use js8a_decode
    implicit none

    class(js8a_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer :: submode
    save

    submode=0
    call js8_decoded(snr, dt, freq, decoded, nap, qual, submode)

    select type(this)
    type is (counting_js8a_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine js8a_decoded

  subroutine js8b_decoded (this,snr,dt,freq,decoded,nap,qual)
    use js8b_decode
    implicit none

    class(js8b_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer :: submode
    save
    
    submode=1
    call js8_decoded(snr, dt, freq, decoded, nap, qual, submode)

    select type(this)
    type is (counting_js8b_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine js8b_decoded

  subroutine js8c_decoded (this,snr,dt,freq,decoded,nap,qual)
    use js8c_decode
    implicit none

    class(js8c_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer :: submode
    save

    submode=2
    call js8_decoded(snr, dt, freq, decoded, nap, qual, submode)

    select type(this)
    type is (counting_js8c_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine js8c_decoded

  subroutine js8e_decoded (this,snr,dt,freq,decoded,nap,qual)
    use js8e_decode
    implicit none

    class(js8e_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer :: submode
    save

    submode=4
    call js8_decoded(snr, dt, freq, decoded, nap, qual, submode)

    select type(this)
    type is (counting_js8e_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine js8e_decoded

  subroutine js8i_decoded (this,snr,dt,freq,decoded,nap,qual)
    use js8i_decode
    implicit none

    class(js8i_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap 
    real, intent(in) :: qual 
    integer :: submode
    save

    submode=8
    call js8_decoded(snr, dt, freq, decoded, nap, qual, submode)

    select type(this)
    type is (counting_js8i_decoder)
       this%decoded = this%decoded + 1
    end select

    return
  end subroutine js8i_decoded

end subroutine multimode_decoder
