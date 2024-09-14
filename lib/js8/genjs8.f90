subroutine genjs8(msg,icos,i3bit,msgsent,msgbits,itone)

! Encode an JS8 message, producing array itone().
  
  use crc
  use packjt

  include 'js8_params.f90'

  parameter (KK=87)                     !Information bits (75 + CRC12)
  parameter (ND=58)                     !Data symbols
  parameter (NS=21)                     !Sync symbols (3 @ Costas 7x7)
  parameter (NN=NS+ND)                  !Total channel symbols (79)
  
  character*68 alphabet
  character*22 msg,msgsent
  character*87 cbits
  integer icos
  integer*4 i4Msg6BitWords(12)                !72-bit message as 6-bit words
  integer*1 msgbits(KK),codeword(3*ND)
  integer*1, target:: i1Msg8BitBytes(11)
  integer itone(NN)

  integer icos7a(0:6), icos7b(0:6), icos7c(0:6)
  if(icos.eq.1) then                            !icos is used elsewhere as NCOSTAS parameter
    icos7a = (/4,2,5,6,1,3,0/)                  !Beginning Costas 7x7 tone pattern
    icos7b = (/4,2,5,6,1,3,0/)                  !Middle Costas 7x7 tone pattern
    icos7c = (/4,2,5,6,1,3,0/)                  !End Costas 7x7 tone pattern
  else
    icos7a = (/0,6,2,3,5,4,1/)                  !Beginning Costas 7x7 tone pattern
    icos7b = (/1,5,0,2,3,6,4/)                  !Middle Costas 7x7 tone pattern
    icos7c = (/2,5,0,6,4,1,3/)                  !End Costas 7x7 tone pattern
  endif

  alphabet='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+/?.'
  
  itype=6
  do i=1,12
    v=index(alphabet, msg(i:i))
    if(v.eq.0) exit
    i4Msg6BitWords(i)=v - 1
  enddo
  msgsent='                      '
  msgsent(1:12)=msg(1:12)

  ! call packmsg(msg,i4Msg6BitWords,itype) !Pack into 12 6-bit bytes
  ! call unpackmsg(i4Msg6BitWords,msgsent) !Unpack to get msgsent

  write(cbits,1000) i4Msg6BitWords,32*i3bit
1000 format(12b6.6,b8.8)
  read(cbits,1001) i1Msg8BitBytes(1:10)
1001 format(10b8)
  i1Msg8BitBytes(10)=iand(i1Msg8BitBytes(10),transfer(128+64+32,0_1))
  i1Msg8BitBytes(11)=0
  icrc12=crc12(c_loc(i1Msg8BitBytes),11)
  icrc12=xor(icrc12, 42) ! TODO: jsherer - could change the crc here

! For reference, here's how to check the CRC
!  i1Msg8BitBytes(10)=icrc12/256
!  i1Msg8BitBytes(11)=iand (icrc12,255)
!  checksumok = crc12_check(c_loc (i1Msg8BitBytes), 11)
!  if( checksumok ) write(*,*) 'Good checksum'

  write(cbits,1003) i4Msg6BitWords,i3bit,icrc12
1003 format(12b6.6,b3.3,b12.12)
  read(cbits,1004) msgbits
1004 format(87i1)

  call encode174(msgbits,codeword)      !Encode the test message

! Message structure: S7 D29 S7 D29 S7
  itone(1:7)=icos7a
  itone(36+1:36+7)=icos7b
  itone(NN-6:NN)=icos7c
  k=7
  do j=1,ND
     i=3*j -2
     k=k+1
     if(j.eq.30) k=k+7
     indx=codeword(i)*4 + codeword(i+1)*2 + codeword(i+2)
     itone(k)=indx
  enddo
  return
end subroutine genjs8
