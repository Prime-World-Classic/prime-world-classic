#pragma once
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//??#include "AIClasses.h"
//??#include "../DebugTools/DebugInfoManager.h"
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EPathRestriction : int; // declared in CommonPathFinder.h

namespace NWorld
{

class TileMap;
class PFBaseMovingUnit;

_interface IPathValidator
{
	// �������� ����; ���� ������ false - ���� �������� �� ������
	virtual bool CheckPath( int prevPathLen, int newPathLen ) = 0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//! ���� �����
_interface IPath
{
	virtual bool IsFinished() const = 0;

	virtual bool CanPeek( int nShift ) const = 0;
	// �������� �����, ��������� �� "�������" �� nShift ������ �����
	virtual const CVec2 PeekPoint( const int nShift ) const = 0;
	// �������� "�������" �� _nShift ������ �����
	virtual void Shift( const int nShift, int numSteps ) = 0;

	virtual const CVec2& GetFinishPoint() const = 0;
	virtual const CVec2& GetStartPoint() const = 0;

	//! ������������ ���� �� ����� ����� ( vPoint )
	virtual void RecoverPath( const CVec2 &vPoint, const SVector &vLastKnownGoodTile, int numSteps ) = 0;
	//! ����������� ���� �� ����� ����� ( vPoint )
	virtual bool RecalcPath( const CVec2 &vPoint, const SVector &vLastKnownGoodTile, IPathValidator *pValidator, int numSteps ) = 0;
	//! �������� ����� � ������ ����
	virtual void InsertTiles( const list<SVector> &tiles ) = 0;
	//! ����� �� �������� ���� ���� �����
//	virtual const bool CanGoBackward( const NWorld::PFBaseMovingUnit *pUnit ) const = 0;
	virtual const bool ShouldCheckTurn() const = 0;
	//! ����� �� ��� ����� ���� ��������� ������� ��������
	virtual const bool CanBuildComplexTurn() const = 0;
//??	virtual void MarkPath( const int nID, const NDebugInfo::EColor color ) const = 0;
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace
