module js8c_decode

  type :: js8c_decoder
     procedure(js8c_decode_callback), pointer :: callback
   contains
     procedure :: decode
  end type js8c_decoder

  abstract interface
     subroutine js8c_decode_callback (this,snr,dt,freq,decoded,nap,qual)
       import js8c_decoder
       implicit none
       class(js8c_decoder), intent(inout) :: this
       integer, intent(in) :: snr
       real, intent(in) :: dt
       real, intent(in) :: freq
       character(len=37), intent(in) :: decoded
       integer, intent(in) :: nap 
       real, intent(in) :: qual 
     end subroutine js8c_decode_callback
  end interface

contains

  subroutine decode(this,callback,iwave,nfqso,newdat,  &
       nutc,nfa,nfb,ndepth,nagain,napwid,syncStats)
!    use wavhdr
    use timer_module, only: timer
!    type(hdr) h
    use js8c_module

    class(js8c_decoder), intent(inout) :: this
    procedure(js8c_decode_callback) :: callback
    real s(NH1,NHSYM)
    real sbase(NH1)
    real candidate(3,NMAXCAND)
    real dd(NMAX)
    logical, intent(in) :: nagain
    logical newdat,lsubtract,ldupe,syncStats
    integer*2 iwave(NMAX)
    integer apsym(KK)
    character datetime*13,message*22,msg37*37
    character*22 allmessages(100)
    integer allsnrs(100)
    save s,dd

    icos=int(NCOSTAS)
    this%callback => callback
    write(datetime,1001) nutc        !### TEMPORARY ###
1001 format("000000_",i6.6)

    dd=iwave
    ndecodes=0
    n2=0
    n3=0
    allmessages='                      '
    allsnrs=0
    ifa=nfa
    ifb=nfb
    if(nagain) then
       ifa=nfqso-10
       ifb=nfqso+10
    endif

    ! For now:
    ! ndepth=1: no subtraction, 1 pass, belief propagation only
    ! ndepth=2: subtraction, 3 passes, belief propagation only
    ! ndepth=3: subtraction, 3 passes, bp+osd
    ! ndepth=4: subtraction, 4 passes, bp+osd
    if(ndepth.eq.1) npass=1
    if(ndepth.eq.2) npass=3
    if(ndepth.ge.3) npass=4

    do ipass=1,npass
      newdat=.true.  ! Is this a problem? I hijacked newdat.
      syncmin=ASYNCMIN
      if(ipass.eq.1) then
        lsubtract=.true.
        if(ndepth.eq.1) lsubtract=.false.
      elseif(ipass.eq.2) then
        n2=ndecodes
        if(ndecodes.eq.0) cycle
        lsubtract=.true.
      elseif(ipass.eq.3) then
        n3=ndecodes
        if((ndecodes-n2).eq.0) cycle
        lsubtract=.true. 
      elseif(ipass.eq.4) then
        if((ndecodes-n3).eq.0) cycle
        lsubtract=.false. 
      endif 

      if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug> pass', ipass, 'of', npass, 'subtract', lsubtract
        flush(6)
      endif

      call timer('syncjs8 ',0)
      call syncjs8(dd,icos,ifa,ifb,syncmin,nfqso,s,candidate,ncand,sbase)
      call timer('syncjs8 ',1)

      if(NWRITELOG.eq.1) then
        write(*,*) '<DecodeDebug>', ncand, "candidates"
        flush(6)
      endif

      do icand=1,ncand
        sync=candidate(3,icand)
        f1=candidate(1,icand)
        xdt=candidate(2,icand)
        xbase=10.0**(0.1*(sbase(nint(f1/(12000.0/NFFT1)))-38.0)) ! 3.125Hz

        if(NWRITELOG.eq.1) then
          write(*,*) '<DecodeDebug> candidate', icand, 'f1', f1, 'sync', sync, 'xdt', xdt, 'xbase', xbase
          flush(6)
        endif

        call timer('js8dec  ',0)
        call js8dec(dd,icos,newdat,syncStats,nfqso,ndepth,napwid,     &
             lsubtract,nagain,iaptype,f1,xdt,xbase,apsym,nharderrors, &
             dmin,nbadcrc,iappass,msg37,xsnr)
        message=msg37(1:22)   !###
        nsnr=nint(xsnr) 
        xdt=xdt-ASTART
        hd=nharderrors+dmin

        if(NWRITELOG.eq.1) then
          write(*,*) '<DecodeDebug> candidate', icand, 'hard', hd, 'nbadcrc', nbadcrc
          flush(6)
        endif
        
        call timer('js8dec  ',1)
        if(nbadcrc.eq.0) then
           ldupe=.false.
           do id=1,ndecodes
              if(message.eq.allmessages(id).and.nsnr.le.allsnrs(id)) ldupe=.true.
           enddo
           if(.not.ldupe) then
              ndecodes=ndecodes+1
              allmessages(ndecodes)=message
              allsnrs(ndecodes)=nsnr
           endif
           if(.not.ldupe .and. associated(this%callback)) then
              qual=1.0-(nharderrors+dmin)/60.0 ! scale qual to [0.0,1.0]
              call this%callback(nsnr,xdt,f1,msg37,iaptype,qual)
           endif
        endif
        
        if(NWRITELOG.eq.1) then
          write(*,*) '<DecodeDebug> ---'
          flush(6)
        endif
      enddo
  enddo
  return
  end subroutine decode

end module js8c_decode
