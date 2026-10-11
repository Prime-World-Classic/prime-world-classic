#pragma once
#include "Server/RPC/Base.h"
#include "SessionPlayerScoring.h"
#include "SessionPlayerExtra.h"
#include "System/npair.h"

//
// ATTENTION!
// AHTUNG!
// � ���� �������� ���� �������� � ���� ����������, C#
// ����� ������ ����������� �������������� � ������������� ������� ����������
//

namespace StatisticService
{
  namespace RPC
  {
    struct SessionClientResultsPlayer : rpc::Data
    {
      SERIALIZE_ID();
      ZDATA
        int userid;
      SessionPlayerScoring scoring;
      SessionPlayerExtra extra;
      ZEND int operator&( IBinSaver &f ) { f.Add(2,&userid); f.Add(3,&scoring); f.Add(4,&extra); return 0; }

      explicit SessionClientResultsPlayer( int _userId = 0 ) :
      userid( _userId )
      {}

      bool operator == ( const SessionClientResultsPlayer & other ) const {
        return 
          ( userid == other.userid ) && 
          ( scoring == other.scoring ) &&
          ( extra == other.extra );
      }
    };



    //
    //��� ��������� ����������� �� ��������, �������� ����������� �� game server,
    //�������� �� ����� � ������������ � ����������
    //����� ����������, ������ �� ��� ������������ � lobby � roll service
    //
    struct SessionClientResults : rpc::Data
    {
      SERIALIZE_ID();
      ZDATA
        int sideWon; //lobby::ETeam::Enum
      int surrenderVote;
      vector<SessionClientResultsPlayer> players;
      // Killer/victim pairs (user ids) collected by the client. Serialized as
      // id 5 (count + raw pair block). NOT part of operator== on purpose:
      // the majority-vote merge compares scoring only, and the pair order may
      // differ between clients.
      vector<nstl::pair<int,int> > playerKills;
      ZEND int operator&( IBinSaver &f ) { f.Add(2,&sideWon); f.Add(3,&surrenderVote); f.Add(4,&players); f.Add(5,&playerKills); return 0; }

      SessionClientResults() :
      sideWon( -1 ), surrenderVote( 0 )
      {}

      bool operator == ( const SessionClientResults & other ) const {
        return 
          ( sideWon == other.sideWon ) &&
          ( surrenderVote == other.surrenderVote ) &&
          ( players == other.players );
      }
    };

  } //namespace RPC

} //namespace StatisticService
