#ifndef SYSTEM_LIFEHACKS_H_INCLUDED
#define SYSTEM_LIFEHACKS_H_INCLUDED

//
//Bright-coloured side
//

#include <vector>

namespace lifehack
{

template <typename TContainer>
typename TContainer::iterator StlErase( TContainer & _cont, typename TContainer::iterator _it )
{
  typename TContainer::iterator result = _it;
  ++result;
  _cont.erase( _it );
  return result;
}



template<typename T>
class EasyVector : public std::vector<T>
{
public:
  explicit EasyVector( T _x0 ) {
    this->push_back( _x0 );
  }

  EasyVector( T _x0, T _x1 ) {
    this->reserve( 2 );
    this->push_back( _x0 );
    this->push_back( _x1 );
  }

  EasyVector( T _x0, T _x1, T _x2 ) {
    this->reserve( 3 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3 ) {
    this->reserve( 4 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4 ) {
    this->reserve( 5 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5 ) {
    this->reserve( 6 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6 ) {
    this->reserve( 7 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7 ) {
    this->reserve( 8 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8 ) {
    this->reserve( 9 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9 ) {
    this->reserve( 10 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9, T _x10 ) {
    this->reserve( 11 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
    this->push_back( _x10 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9, T _x10, T _x11 ) {
    this->reserve( 12 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
    this->push_back( _x10 );
    this->push_back( _x11 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9, T _x10, T _x11, T _x12 ) {
    this->reserve( 13 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
    this->push_back( _x10 );
    this->push_back( _x11 );
    this->push_back( _x12 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9, T _x10, T _x11, T _x12, T _x13 ) {
    this->reserve( 14 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
    this->push_back( _x10 );
    this->push_back( _x11 );
    this->push_back( _x12 );
    this->push_back( _x13 );
  }

  EasyVector( T _x0, T _x1, T _x2, T _x3, T _x4, T _x5, T _x6, T _x7, T _x8, T _x9, T _x10, T _x11, T _x12, T _x13, T _x14 ) {
    this->reserve( 15 );
    this->push_back( _x0 );
    this->push_back( _x1 );
    this->push_back( _x2 );
    this->push_back( _x3 );
    this->push_back( _x4 );
    this->push_back( _x5 );
    this->push_back( _x6 );
    this->push_back( _x7 );
    this->push_back( _x8 );
    this->push_back( _x9 );
    this->push_back( _x10 );
    this->push_back( _x11 );
    this->push_back( _x12 );
    this->push_back( _x13 );
    this->push_back( _x14 );
  }
};

} //namespace lifehack

#endif //SYSTEM_LIFEHACKS_H_INCLUDED
