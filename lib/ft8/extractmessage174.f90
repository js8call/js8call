subroutine extractmessage174(decoded,msgreceived,ncrcflag)
  use iso_c_binding, only: c_loc,c_size_t
  use crc
  use packjt
  character*68 alphabet
  character*22 msgreceived
  character*87 cbits
  integer*1 decoded(87)
  integer*1, target::  i1Dec8BitBytes(11)
  integer*4 i4Dec6BitWords(12)

  alphabet='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+/?.'

! Write decoded bits into cbits: 75-bit message plus 12-bit CRC
  write(cbits,1000) decoded
1000 format(87i1)
  read(cbits,1001) i1Dec8BitBytes
1001 format(11b8)
  read(cbits,1002) ncrc12                         !Received CRC12
1002 format(75x,b12)

  i1Dec8BitBytes(10)=iand(i1Dec8BitBytes(10),transfer(128+64+32,0_1))
  i1Dec8BitBytes(11)=0
  icrc12=crc12(c_loc(i1Dec8BitBytes),11)          !CRC12 computed from 75 msg bits
  icrc12=xor(icrc12, 42) ! TODO: jsherer - could change the crc here

  if(ncrc12.eq.icrc12) then ! .or. sum(decoded(57:87)).eq.0) then  !### Kludge ###
! CRC12 checks out --- unpack 72-bit message
    do ibyte=1,12
      itmp=0
      do ibit=1,6
        itmp=ishft(itmp,1)+iand(1_1,decoded((ibyte-1)*6+ibit))
      enddo
      i4Dec6BitWords(ibyte)=itmp
    enddo

    !call unpackmsg(i4Dec6BitWords,msgreceived)

    msgreceived='                      '
    do ibyte=1,12
      itmp=i4Dec6BitWords(ibyte)
      msgreceived(ibyte:ibyte) = alphabet(itmp+1:itmp+1)
    enddo
    
    ncrcflag=1
  else
    msgreceived=' '
    ncrcflag=-1
  endif 
  return
  end subroutine extractmessage174
