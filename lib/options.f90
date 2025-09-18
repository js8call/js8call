module options
  !
  ! Source code copied from:
  ! http://fortranwiki.org/fortran/show/Command-line+arguments
  !
  implicit none

  type option
     !> Long name.
     character(len=100) :: name
     !> Does the option require an argument?
     logical :: has_arg
     !> Corresponding short name.
     character :: chr
     !> Description.
     character(len=500) :: descr
     !> Argument name, if required.
     character(len=20) :: argname
   contains
     procedure :: print => print_opt
  end type option

contains

  !> Parse command line options. Options and their arguments must come before
  !> all non-option arguments. Short options have the form "-X", long options
  !> have the form "--XXXX..." where "X" is any character. Parsing can be
  !> stopped with the option '--'.
  !> The following code snippet illustrates the intended use:
  !> \code
  !> do
  !>   call getopt (..., optchar=c, ...)
  !>   if (stat /= 0) then
  !>     ! optional error handling
  !>     exit
  !>   end if
  !>   select case (c)
  !>     ! process options
  !>   end select
  !> end do
  !> \endcode
  subroutine getopt (options, longopts, optchar, optarg, arglen, stat, &
       offset, remain, err)
    use iso_fortran_env, only: error_unit

    !> String containing the characters that are valid short options. If
    !> present, command line arguments are scanned for those options.
    !> If a character is followed by a colon (:) its corresponding option
    !> requires an argument. E.g. "vn:" defines two options -v and -n with -n
    !> requiring an argument.
    character(len=*), intent(in), optional :: options

    !> Array of long options. If present, options of the form '--XXXX...' are
    !> recognised. Each option has an associated option character. This can be
    !> any character of default kind, it is just an identifier. It can, but
    !> doesn't have to, match any character in the options argument. In fact it
    !> is possible to only pass long options and no short options at all.
    !> Only name, has_arg and chr need to be set.
    type(option), intent(in), optional :: longopts(:)

    !> If stat is not 1, optchar contains the option character that was parsed.
    !> Otherwise its value is undefined.
    character, intent(out), optional :: optchar

    !> If stat is 0 and the parsed option requires an argument, optarg contains
    !> the first len(optarg) (but at most 500) characters of that argument.
    !> Otherwise its value is undefined. If the arguments length exceeds 500
    !> characters and err is .true., a warning is issued.
    character(len=*), intent(out), optional :: optarg

    !> If stat is 0 and the parsed option requires an argument, arglen contains
    !> the actual length of that argument. Otherwise its value is undefined.
    !> This can be used to make sure the argument was not truncated by the
    !> limited length of optarg.
    integer, intent(out), optional :: arglen

    !> Status indicator. Can have the following values:
    !>   -  0: An option was successfully parsed.
    !>   -  1: Parsing stopped successfully because a non-option or '--' was
    !>         encountered.
    !>   - -1: An unrecognised option was encountered.
    !>   - -2: A required argument was missing.
    !>   .
    !> Its value is never undefined.
    integer, intent(out), optional :: stat

    !> If stat is 1, offset contains the number of the argument before the
    !> first non-option argument, i.e. offset+n is the nth non-option argument.
    !> If stat is not 1, offset contains the number of the argument that would
    !> be parsed in the next call to getopt. This number can be greater than
    !> the actual number of arguments.
    integer, intent(out), optional :: offset

    !> If stat is 1, remain contains the number of remaining non-option
    !> arguments, i.e. the non-option arguments are in the range
    !> (offset+1:offset+remain). If stat is not 1, remain is undefined.
    integer, intent(out), optional :: remain

    !> If err is present and .true., getopt prints messages to the standard
    !> error unit if an error is encountered (i.e. whenever stat would be set
    !> to a negative value).
    logical, intent(in), optional :: err

    integer, save :: pos = 1, cnt = 0
    character(len=500), save :: arg

    integer :: chrpos, length, st, id = 0
    character :: chr
    logical :: long

    if (cnt == 0) cnt = command_argument_count()
    long = .false.

    ! no more arguments left
    if (pos > cnt) then
       pos = pos - 1
       st = 1
       goto 10
    end if

    call get_command_argument (pos, arg, length)

    ! is argument an option?
    if (arg(1:1) == '-') then

       chr = arg(2:2)

       ! too long ('-xxxx...') for one dash?
       if (chr /= '-' .and. len_trim(arg) > 2) then
          st = -1
          goto 10
       end if

       ! forced stop ('--')
       if (chr == '-' .and. arg(3:3) == ' ') then
          st = 1
          goto 10
       end if

       ! long option ('--xxx...')
       if (chr == '-') then

          long = .true.

          ! check if valid
          id = lookup(arg(3:))

          ! option is invalid, stop
          if (id == 0) then
             st = -1
             goto 10
          end if

          chr = longopts(id)%chr

          ! check if option requires an argument
          if (.not. longopts(id)%has_arg) then
             st = 0
             goto 10
          end if

          ! check if there are still arguments left
          if (pos == cnt) then
             st = -2
             goto 10
          end if

          ! go to next position
          pos = pos + 1

          ! get argument
          call get_command_argument (pos, arg, length)

          ! make sure it is not an option
          if (arg(1:1) == '-') then
             st = -2
             pos = pos - 1
             goto 10
          end if

       end if

       ! short option
       ! check if valid
       if (present(options)) then
          chrpos = scan(options, chr)
       else
          chrpos = 0
       end if

       ! option is invalid, stop
       if (chrpos == 0) then
          st = -1
          goto 10
       end if

       ! look for argument requirement
       if (chrpos < len_trim(options)) then
          if (options(chrpos+1:chrpos+1) == ':') then

             ! check if there are still arguments left
             if (pos == cnt) then
                st = -2
                goto 10
             end if

             ! go to next position
             pos = pos + 1

             ! get argument
             call get_command_argument (pos, arg, length)

             ! make sure it is not an option
             if (arg(1:1) == '-') then
                st = -2
                pos = pos - 1
                goto 10
             end if

          end if
       end if

       ! if we get to this point, no error happened
       ! return option and the argument (if there is one)
       st = 0
       goto 10
    end if

    ! not an option, parsing stops
    st = 1
    ! we are already at the first non-option argument
    ! go one step back to the last option or option argument
    pos = pos - 1


    ! error handling and setting of return values
10  continue

    if (present(err)) then
       if (err) then

          select case (st)
          case (-1)
             write (error_unit, *) "error: unrecognised option: " // trim(arg)
          case (-2)
             if (.not. long) then
                write (error_unit, *) "error: option -" // chr &
                     // " requires an argument"
             else
                write (error_unit, *) "error: option --" &
                     // trim(longopts(id)%name) // " requires an argument"
             end if
          end select

       end if
    end if

    if (present(optchar)) optchar = chr
    if (present(optarg))  optarg  = arg
    if (present(arglen))  arglen  = length
    if (present(stat))    stat    = st
    if (present(offset))  offset  = pos
    if (present(remain))  remain  = cnt-pos

    ! setup pos for next call to getopt
    pos = pos + 1

  contains

    integer function lookup (name)
      character(len=*), intent(in) :: name
      integer :: i

      ! if there are no long options, skip the loop
      if (.not. present(longopts)) goto 10

      do i = 1, size(longopts)
         if (name == longopts(i)%name) then
            lookup = i
            return
         end if
      end do
      ! if we get to this point, the option was not found

10    lookup = 0
    end function lookup

  end subroutine getopt

  !============================================================================

  !> Print an option in the style of a man page. I.e.
  !> \code
  !> -o arg
  !> --option arg
  !>    description.................................................................
  !>    ............................................................................
  !> \endcode
  subroutine print_opt (opt, unit)
    !> the option
    class(option), intent(in) :: opt
    !> logical unit number
    integer, intent(in) :: unit

    integer :: l, c1, c2

    if (opt%has_arg) then
       write (unit, '(1x,"-",a,1x,a)') opt%chr, trim(opt%argname)
       write (unit, '(1x,"--",a,1x,a)') trim(opt%name), trim(opt%argname)
    else
       write (unit, '(1x,"-",a)') opt%chr
       write (unit, '(1x,"--",a)') trim(opt%name)
    end if
    l = len_trim(opt%descr)

    ! c1 is the first character of the line
    ! c2 is one past the last character of the line
    c1 = 1
    do
       if (c1 > l) exit
       ! print at maximum 4+76 = 80 characters
       c2 = min(c1 + 76, 500)
       ! if not at the end of the whole string
       if (c2 /= 500) then
          ! find the end of a word
          do
             if (opt%descr(c2:c2) == ' ') exit
             c2 = c2-1
          end do
       end if
       write (unit, '(4x,a)') opt%descr(c1:c2-1)
       c1 = c2+1
    end do

  end subroutine print_opt

end module options
