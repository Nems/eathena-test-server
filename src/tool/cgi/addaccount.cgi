#!/usr/bin/perl

#=========================================================================
# addaccount.cgi  ver.1.00
#	ladmin�����b�v�����A�A�J�E���g���쐬����CGI�B
#	ladmin ver.1.04�ł̓�����m�F�B
#
# ** �ݒ���@ **
#
# - ����$ladmin�ϐ���ladmin�ւ̃p�X��ݒ肷�邱�ƁB
# - UNIX�nOS�Ŏg�p����ꍇ��ladmin�Ƌ��ɉ��s�R�[�h��ϊ����邱�ƁA�܂�
#   �t�@�C���擪�s��perl�̐������p�X�ɂ��邱�ƁB��> $ which perl
# - �T�[�o�[�v���O������u���E�U�ɂ���Ă� $cgiuri �ɂ��̃t�@�C���ւ�
#   ���S��URI���Z�b�g���Ȃ���΂Ȃ�Ȃ��ꍇ������B
# - perl�Ƀp�X���ʂ��Ă��Ȃ��ꍇ�� $perl ��perl�ւ̐������p�X�ɂ��邱�ƁB
# - ���͕��ʂ�CGI�Ɠ����ł��B�i���s����cgi-bin�t�H���_�Ȃǁj
#
# ** ���̑� **
#   addaccount.cgi ���u���E�U�ŊJ���ƃT���v��HTML�i���̂܂܎g���܂��j��
#   �J���܂��B�܂��A����cgi�̓u���E�U���瑗����Accept-Language��
#   ja�Ŏn�܂��Ă���΃��b�Z�[�W�̈ꕔ����{��ɕϊ����܂��B
#   (IE�Ȃ�C���^�[�l�b�g�I�v�V�����̌���ݒ�ň�ԏ�ɓ��{���u��)
#	����ȊO�̏ꍇ�͉p��̂܂܏o�͂��܂��B
#-------------------------------------------------------------------------

my($ladmin)	= "../ladmin";			# ladmin�̃p�X(�����炭�ύX���K�v)

my($cgiuri)	= "./addaccount.cgi";	# ���̃t�@�C����URI
my($perl)	= "perl";				# perl�̃R�}���h��



#--------------------------- �ݒ肱���܂� --------------------------------






use strict;
use CGI;

my($cgi)= new CGI;
my(%langconv)=(
	'Athena login-server administration tool.*' => '',
	'logged on.*' => '',
);

# ----- ���{����Ȃ�ϊ��e�[�u�����Z�b�g -----
if($ENV{'HTTP_ACCEPT_LANGUAGE'}=~/^ja/){
	my(%tmp)=(
		'Account \[(.+)\] is successfully created.*'
			=> '�A�J�E���g "$1" ���쐬���܂���.',
		'Account \[(.+)\] creation failed\. same account exists.*'
			=> '�A�J�E���g "$1" �͊��ɑ��݂��܂�.',
		'Illeagal charactor found in UserID.*'
			=> 'ID�̒��ɕs���ȕ���������܂�.',
		'Illeagal charactor found in Password.*'
			=> 'Password�̒��ɕs���ȕ���������܂�.',
		'input UserID 4-24 bytes.'
			=> 'ID�͔��p4�`24�����œ��͂��Ă�������.',
		'input Password 4-24 bytes.'
			=> 'Password�͔��p4�`24�����œ��͂��Ă�������.',
		'Illeagal gender.*'
			=> '���ʂ����������ł�.',
		'Cant connect to login server.*'
			=> '���O�C���T�[�o�[�ɐڑ��ł��܂���.',
		'login error.*'
			=> '���O�C���T�[�o�[�ւ̊Ǘ��Ҍ������O�C���Ɏ��s���܂���',
		"Can't execute ladmin.*"
			=> 'ladmin�̎��s�Ɏ��s���܂���',
		'UserID "(.+)" is already used.*'
			=> 'ID "$1" �͊��Ɏg�p����Ă��܂�.',
		'You can use UserID \"(.+)\".*'
			=> 'ID "$1" �͎g�p�\�ł�.',
		
		'account making'	=>'�A�J�E���g�쐬',
		'\>UserID'			=>'>�h�c',
		'\>Password'		=>'>�p�X���[�h',
		'\>Gender'			=>'>����',
		'\>Male'			=>'>�j��',
		'\>Female'			=>'>����',
		'\"Make Account\"'	=>'"�A�J�E���g�쐬"',
		'\"Check UserID\"'	=>'"ID�̃`�F�b�N"',
	);
	map { $langconv{$_}=$tmp{$_}; } keys (%tmp);
}

# ----- �ǉ� -----
if( $cgi->param("addaccount") ){
	my($userid)= $cgi->param("userid");
	my($passwd)= $cgi->param("passwd");
	my($gender)= lc(substr($cgi->param("gender"),0,1));
	if(length($userid)<4 || length($userid)>24){
		HttpError("input UserID 4-24 bytes.");
	}
	if(length($passwd)<4 || length($passwd)>24){
		HttpError("input Password 4-24 bytes.");
	}
	if($userid=~/[^0-9A-Za-z\@\_\-\']/){
		HttpError("Illeagal charactor found in UserID.");
	}
	if($passwd=~/[\x00-\x1f\x80-\xff\']/){
		HttpError("Illeagal charactor found in Password.");
	}
	if($gender!~/[mf]/){
		HttpError("Gender error.");
	}
	open PIPE,"$perl $ladmin --add $userid $gender $passwd |"
		or HttpError("Can't execute ladmin.");
	my(@msg)=<PIPE>;
	close PIPE;
	HttpMsg(@msg);
}
# ----- ���݃`�F�b�N -----
elsif( $cgi->param("check") ){
	my($userid)= $cgi->param("userid");
	if(length($userid)<4 || length($userid)>24){
		HttpError("input UserID 4-24 bytes.");
	}
	if($userid=~/[^0-9A-Za-z\@\_\-\']/){
		HttpError("Illeagal charactor found in UserID.");
	}
	open PIPE,"$perl $ladmin --search --regex \\b$userid\\b |"
		or HttpError("Can't execute ladmin.");
	my(@msg)=<PIPE>;
	close PIPE;
	if(scalar(@msg)==6 && (split /[\s\0]+/,substr($msg[4],11,24))[0] eq $userid){
		HttpMsg("NG : UserID \"$userid\" is already used.");
	}elsif(scalar(@msg)==5){
		HttpMsg("OK : You can use UserID \"$userid\"");
	}
	HttpError("ladmin error ?\n---output---\n",@msg);
}

# ----- �t�H�[�� -----
else{
	print LangConv( <<"EOM" );
Content-type: text/html\n
<html>
 <head>
  <title>Athena account making cgi</title>
 </head>
 <body>
  <h1>Athena account making cgi</h1>
  <form action="$cgiuri" method="post">
   <table border=2>
    <tr>
     <th>UserID</th>
     <td><input name="userid" size=24 maxlength=24></td>
    </tr>
    <tr>
     <th>Password</th>
     <td><input name="passwd" size=24 maxlength=24 type="password"></td>
    </tr>
    <tr>
     <th>Gender</th>
     <td>
      <input type="radio" name="gender" value="male">Male
      <input type="radio" name="gender" value="female">Female
     </td>
    </tr>
    <tr>
     <td colspan=2>
      <input type="submit" name="addaccount" value="Make Account">
      <input type="submit" name="check" value="Check UserID">
     </td>
    </tr>
   </table>
  </form>
 </body>
</html>
EOM
	exit;
}

sub LangConv {
	my(@lst)= @_;
	my($a,$b,@out)=();
	foreach $a(@lst){
		foreach $b(keys %langconv){
			$a=~s/$b/$langconv{$b}/g;
			my($rep1)=$1;
			$a=~s/\$1/$rep1/g;
		}
		push @out,$a;
	}
	return @out;
}

sub HttpMsg {
	my($msg)=join("", LangConv(@_));
	$msg=~s/\n/<br>\n/g;
	print LangConv("Content-type: text/html\n\n"),$msg;
	exit;
}

sub HttpError {
	my($msg)=join("", LangConv(@_));
	$msg=~s/\n/<br>\n/g;
	print LangConv("Content-type: text/html\n\n"),$msg;
	exit;
}

