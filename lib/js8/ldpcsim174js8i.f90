program ldpcsim174js8
! End to end test of the (174,75)/crc12 encoder and decoder.
use crc
use packjt

include 'js8_params.f90'
include 'js8i_params.f90'

character*22 msg,msgsent,msgreceived
character*8 arg
integer*1, allocatable ::  codeword(:), decoded(:), message(:)
integer*1, target:: i1Msg8BitBytes(11)
integer*1 msgbits(87)
integer*1 apmask(174), cw(174)
integer*2 checksum
integer*4 i4Msg6BitWords(13)
integer colorder(174)
integer nerrtot(174),nerrdec(174),nmpcbad(87)
logical checksumok,fsk,bpsk
real*8, allocatable ::  rxdata(:)
real, allocatable :: llr(:)

data colorder/            &
   0,  1,  2,  3, 30,  4,  5,  6,  7,  8,  9, 10, 11, 32, 12, 40, 13, 14, 15, 16,&
  17, 18, 37, 45, 29, 19, 20, 21, 41, 22, 42, 31, 33, 34, 44, 35, 47, 51, 50, 43,&
  36, 52, 63, 46, 25, 55, 27, 24, 23, 53, 39, 49, 59, 38, 48, 61, 60, 57, 28, 62,&
  56, 58, 65, 66, 26, 70, 64, 69, 68, 67, 74, 71, 54, 76, 72, 75, 78, 77, 80, 79,&
  73, 83, 84, 81, 82, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99,&
 100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,&
 120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,&
 140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,&
 160,161,162,163,164,165,166,167,168,169,170,171,172,173/

nerrtot=0
nerrdec=0
nmpcbad=0  ! Used to collect the number of errors in the message+crc part of the codeword

nargs=iargc()
if(nargs.ne.4) then
   print*,'Usage: ldpcsim  niter  ndepth  #trials   s '
   print*,'eg:    ldpcsim    10     2      1000    0.84'
   print*,'belief propagation iterations: niter, ordered-statistics depth: ndepth'
   print*,'If s is negative, then value is ignored and sigma is calculated from SNR.'
   return
endif
call getarg(1,arg)
read(arg,*) max_iterations 
call getarg(2,arg)
read(arg,*) ndepth 
call getarg(3,arg)
read(arg,*) ntrials 
call getarg(4,arg)
read(arg,*) s

fsk=.false.
bpsk=.true.

! don't count crc bits as data bits
N=174
K=87
! scale Eb/No for a (174,87) code
rate=real(K)/real(N)

write(*,*) "rate: ",rate
write(*,*) "niter= ",max_iterations," s= ",s

allocate ( codeword(N), decoded(K), message(K) )
allocate ( rxdata(N), llr(N) )

  msg="0123456789012"
!  msg="G4WJS K9AN EN50"
  call packmsg(msg,i4Msg6BitWords,itype) !Pack into 12 6-bit bytes
  call unpackmsg(i4Msg6BitWords,msgsent) !Unpack to get msgsent
  write(*,*) "message sent ",msgsent

  i4=0
  ik=0
  im=0
  do i=1,12
    nna=i4Msg6BitWords(i)
    do j=1, 6
      ik=ik+1
      i4=i4+i4+iand(1,ishft(nna,j-6))
      i4=iand(i4,255)
      if(ik.eq.8) then
        im=im+1
!           if(i4.gt.127) i4=i4-256
        i1Msg8BitBytes(im)=i4
        ik=0
      endif
    enddo
  enddo

  i1Msg8BitBytes(10:11)=0
  checksum = crc12 (c_loc (i1Msg8BitBytes), 11)
  checksum = xor(checksum, 42_2) ! TODO: jsherer - could change the crc here
! For reference, the next 3 lines show how to check the CRC
  i1Msg8BitBytes(10)=checksum/256
  i1Msg8BitBytes(11)=iand(checksum,transfer(255,0_2))
  checksumok = crc12_check(c_loc (i1Msg8BitBytes), 11)
  if( checksumok ) write(*,*) 'Good checksum'

! K=87, For now: 
! msgbits(1:72) JT message bits
! msgbits(73:75) 3 free message bits (set to 0) 
! msgbits(76:87) CRC12
  mbit=0
  do i=1, 9 
    i1=i1Msg8BitBytes(i)
    do ibit=1,8
      mbit=mbit+1
      msgbits(mbit)=iand(1,ishft(i1,ibit-8))
    enddo
  enddo
  msgbits(73:75)=0  ! the three extra message bits go here
  i1=i1Msg8BitBytes(10) ! First 4 bits of crc12 are LSB of this byte
  do ibit=1,4
    msgbits(75+ibit)=iand(1,ishft(i1,ibit-4))
  enddo
  i1=i1Msg8BitBytes(11) ! Now shift in last 8 bits of the CRC
  do ibit=1,8
    msgbits(79+ibit)=iand(1,ishft(i1,ibit-8))
  enddo

  write(*,*) 'message'
  write(*,'(11(8i1,1x))') msgbits

  call encode174(msgbits,codeword)
  call init_random_seed()
!  call sgran()

  write(*,*) 'codeword' 
  write(*,'(22(8i1,1x))') codeword

write(*,*) "Es/N0   SNR2500   ngood  nundetected nbadcrc   sigma"
do idb = 20,-10,-1 
!do idb = -3,-3,-1 
  db=idb/2.0-1.0
  sigma=1/sqrt( 2*(10**(db/10.0)) )
  ngood=0
  nue=0
  nbadcrc=0
  nberr=0
  do itrial=1, ntrials
! Create a realization of a noisy received word
    do i=1,N
      if( bpsk ) then
        rxdata(i) = 2.0*codeword(i)-1.0 + sigma*gran()
      elseif( fsk ) then
        if( codeword(i) .eq. 1 ) then
          r1=(1.0 + sigma*gran())**2 + (sigma*gran())**2
          r2=(sigma*gran())**2 + (sigma*gran())**2
        elseif( codeword(i) .eq. 0 ) then
          r2=(1.0 + sigma*gran())**2 + (sigma*gran())**2
          r1=(sigma*gran())**2 + (sigma*gran())**2
        endif 
!        rxdata(i)=0.35*(sqrt(r1)-sqrt(r2))
!        rxdata(i)=0.35*(exp(r1)-exp(r2))
        rxdata(i)=0.12*(log(r1)-log(r2))
      endif
    enddo
    nerr=0
    do i=1,N
      if( rxdata(i)*(2*codeword(i)-1.0) .lt. 0 ) nerr=nerr+1
    enddo
    if(nerr.ge.1) nerrtot(nerr)=nerrtot(nerr)+1
    nberr=nberr+nerr

! Correct signal normalization is important for this decoder.
    rxav=sum(rxdata)/N
    rx2av=sum(rxdata*rxdata)/N
    rxsig=sqrt(rx2av-rxav*rxav)
    rxdata=rxdata/rxsig
! To match the metric to the channel, s should be set to the noise standard deviation. 
! For now, set s to the value that optimizes decode probability near threshold. 
! The s parameter can be tuned to trade a few tenth's dB of threshold for an order of
! magnitude in UER 
    if( s .lt. 0 ) then
      ss=sigma
    else 
      ss=s
    endif

    llr=2.0*rxdata/(ss*ss)
    nap=0 ! number of AP bits
    llr(colorder(174-87+1:174-87+nap)+1)=5*(2.0*msgbits(1:nap)-1.0)
    apmask=0
    apmask(colorder(174-87+1:174-87+nap)+1)=1

! max_iterations is max number of belief propagation iterations
    call bpdecode174(llr, apmask, max_iterations, decoded, cw, nharderrors,niterations)
    if( ndepth .ge. 0 .and. nharderrors .lt. 0 ) call osd174(llr, apmask, ndepth, decoded, cw,  nharderrors, dmin)
! If the decoder finds a valid codeword, nharderrors will be .ge. 0.
    if( nharderrors .ge. 0 ) then
      call extractmessage174(decoded,msgreceived,ncrcflag)
      if( ncrcflag .ne. 1 ) then
        nbadcrc=nbadcrc+1
      endif

      nueflag=0
      nerrmpc=0
      do i=1,K   ! find number of errors in message+crc part of codeword
        if( msgbits(i) .ne. decoded(i) ) then
          nueflag=1
          nerrmpc=nerrmpc+1 
        endif
      enddo
      if(nerrmpc.ge.1) nmpcbad(nerrmpc)=nmpcbad(nerrmpc)+1
      if( ncrcflag .eq. 1 ) then
        if( nueflag .eq. 0 ) then
          ngood=ngood+1
          if(nerr.ge.1) nerrdec(nerr)=nerrdec(nerr)+1
        else if( nueflag .eq. 1 ) then
          nue=nue+1;
        endif
      endif
    endif
  enddo
  baud=12000.0/NSPS
  snr2500=db+10.0*log10((baud/2500.0))
  pberr=real(nberr)/(real(ntrials*N))
  write(*,"(f4.1,4x,f5.1,1x,i8,1x,i8,1x,i8,8x,f5.2,8x,e10.3)") db,snr2500,ngood,nue,nbadcrc,ss,pberr

enddo

open(unit=23,file='nerrhisto.dat',status='unknown')
do i=1,174
  write(23,'(i4,2x,i10,i10,f10.2)') i,nerrdec(i),nerrtot(i),real(nerrdec(i))/real(nerrtot(i)+1e-10)
enddo
close(23)
open(unit=25,file='nmpcbad.dat',status='unknown')
do i=1,87
  write(25,'(i4,2x,i10)') i,nmpcbad(i)
enddo
close(25)

end program ldpcsim174js8
