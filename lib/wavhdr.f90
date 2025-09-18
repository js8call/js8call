module wavhdr
  type hdr
     character*4 ariff
     integer*4 lenfile
     character*4 awave
     character*4 afmt
     integer*4 lenfmt
     integer*2 nfmt2
     integer*2 nchan2
     integer*4 nsamrate
     integer*4 nbytesec
     integer*2 nbytesam2
     integer*2 nbitsam2
     character*4 adata
     integer*4 ndata
  end type hdr

  contains

    function default_header(nsamrate,npts)
      type(hdr) default_header,h
      h%ariff='RIFF'
      h%awave='WAVE'
      h%afmt='fmt '
      h%lenfmt=16
      h%nfmt2=1
      h%nchan2=1
      h%nsamrate=nsamrate
      h%nbitsam2=16
      h%nbytesam2=h%nbitsam2 * h%nchan2 / 8
      h%adata='data'
      h%nbytesec=h%nsamrate * h%nbitsam2 * h%nchan2 / 8
      h%ndata=2*npts
      h%lenfile=h%ndata + 44 - 8
      default_header=h
    end function default_header

    subroutine set_wsjtx_wav_params(fMHz,mode,nsubmode,ntrperiod,id2)

      parameter (NBANDS=23,NMODES=11)
      character*8 mode,modes(NMODES)
      integer*2 id2(4)
      integer iperiod(7)
      real fband(NBANDS)
      data fband/0.137,0.474,1.8,3.5,5.1,7.0,10.14,14.0,18.1,21.0,24.9,  &
           28.0,50.0,144.0,222.0,432.0,902.0,1296.0,2304.0,3400.0,       &
           5760.0,10368.0,24048.0/
      data modes/'Echo','FSK441','ISCAT','JT4','JT65','JT6M','JT9',      &
           'JT9+JT65','JTMS','JTMSK','WSPR'/
      data iperiod/5,10,15,30,60,120,900/

      dmin=1.e30
      iband=0
      do i=1,NBANDS
         if(abs(fMHz-fband(i)).lt.dmin) then
            dmin=abs(fMHz-fband(i))
            iband=i
         endif
      enddo

      imode=0
      do i=1,NMODES
         if(mode.eq.modes(i)) imode=i
      enddo

      ip=0
      do i=1,7
         if(ntrperiod.eq.iperiod(i)) ip=i
      enddo

      id2(1)=iband
      id2(2)=imode
      id2(3)=nsubmode
      id2(4)=ip

      return
    end subroutine set_wsjtx_wav_params

    subroutine get_wsjtx_wav_params(id2,band,mode,nsubmode,ntrperiod,ok)

      parameter (NBANDS=23,NMODES=11)
      character*8 mode,modes(NMODES)
      character*6 band,bands(NBANDS)
      integer*2 id2(4)
      integer iperiod(7)
      logical ok
      data modes/'Echo','FSK441','ISCAT','JT4','JT65','JT6M','JT9',    &
           'JT9+JT65','JTMS','JTMSK','WSPR'/
      data iperiod/5,10,15,30,60,120,900/
      data bands/'2190m','630m','160m','80m','60m','40m','30m','20m',  &
           '17m','15m','12m','10m','6m','2m','1.25m','70cm','33cm',    &
           '23cm','13cm','9cm','6cm','3cm','1.25cm'/

      ok=.true.
      if(id2(1).lt.1 .or. id2(1).gt.NBANDS) ok=.false.
      if(id2(2).lt.1 .or. id2(2).gt.NMODES) ok=.false.
      if(id2(3).lt.1 .or. id2(3).gt.8) ok=.false.
      if(id2(4).lt.1 .or. id2(4).gt.7) ok=.false.

      if(ok) then
         band=bands(id2(1))
         mode=modes(id2(2))
         nsubmode=id2(3)
         ntrperiod=iperiod(id2(4))
      endif

      return
    end subroutine get_wsjtx_wav_params

end module wavhdr
