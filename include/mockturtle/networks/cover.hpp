/* mockturtle: C++ logic network library
 * Copyright (C) 2018-2021  EPFL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*!
  \file cover.hpp
  \brief single output cover logic network implementation
  \author Andrea Costamagna
*/

#pragma once

#include "../traits.hpp"
#include "../utils/algorithm.hpp"

#include "detail/foreach.hpp"
#include "events.hpp"
#include "storage.hpp"

#include <kitty/constructors.hpp>
#include <kitty/dynamic_truth_table.hpp>
#include <kitty/print.hpp>

#include <algorithm>

namespace mockturtle
{
/*! \brief cover storage data
 * 
 * This struct contains the constituents of the network and its main features.
 * For what concerns the features, these include:  
 * `num_pis`          : Number of primary inputs
 * `num_pos`          : Number of primary outputs
 * 
 * On the other hand, the constituents of the network are the covers representing the boolean functions stored in each node.
 * These are stored in a vector of pairs. Each element is the cover of a function and a boolean value indicating whether the 
 * cover indicates the ON-set or the OFF set. More precisely:
 * `covers`           : Vector of pairs for covers storage
 * `covers[i].first`  : Cubes i-th cover
 * `covers[i].second` : Boolean true (false) if ON set (OFF set)
 * This data structure directly originates from the k-LUT one and, for this reason, it inherits from it the vast majority of the features. 
 * The main difference is the way the nodes are stored and future improvements could include the substitution of the current covers storage with 
 * a cache, to avoid the redundant storage of some recurrent boolean functions. 
 */
struct cover_storage_data
{
  uint64_t insert( std::pair<std::vector<kitty::cube>, bool> const& cover )
  {
    const auto index = covers.size();
    covers.emplace_back( cover );
    return index;
  }

  std::vector<std::pair<std::vector<kitty::cube>, bool>> covers;
  uint32_t num_pis = 0u;
  uint32_t num_pos = 0u;
  std::vector<int8_t> latches;
  uint32_t trav_id = 0u;
};

/*! \brief cover node
 *
 * The cover node is a mixed fanin node with the following attributes:
 * `children`  : vector of pointers to children
 * `data[0].h1`: Fan-out size
 * `data[0].h2`: Application-specific value
 * `data[1].h1`: Index of the cover of the node in the covers container
 * `data[1].h2`: Visited flags
 */
struct cover_storage_node : mixed_fanin_node<2>
{
  bool operator==( cover_storage_node const& other ) const
  {
    return data[1].h1 == other.data[1].h1 && children == other.children;
  }
};

/*! \brief cover storage container
 *
 * The network as a storage entity is defined by combining the node structure with the cover_storage structure.
 * The attributes of this storage unit are listed in the following:
 * `nodes`            : Vector of cover storage nodes
 * `inputs`           : Vector of indeces to inputs nodes
 * `outputs`          : Vector of pointers to node types
 * `hash`             : maps a node to its index in the nodes vector
 * `data`             : cover storage data
 */
using cover_storage = storage<cover_storage_node, cover_storage_data>;

/*! \brief cover_network
 *
 * This class implements a data structure for a cover based network. 
 * In this representation, each node is represented by specifying its ON set or its OFF set, that in both cases are stored as a vector of cubes.
 * The information related to the set to which the node refers to is contained in a boolean variable, that is true (false) if the
 * ON set (OFF set) is considered. All the basic network methods are implemented and tested but one should note the following things:
 * - Contrarily to the AIG network, and similarly to the k-LUT network, it is not yet implemented the possibility to negate signals while defining a gate;
 * - The methods relative to the latches manipulation need a more extensive testing before being safely used.
 * This data structure is primarily meant to be used for reading .blif files in which the number of variables would make it unfeasible the reading via a k-LUT network.
 * 
  \verbatim embed:rst

  Example

  .. code-block:: c++

    cover_network cover;

    const auto a = cover.create_pi();
    const auto b = cover.create_pi();
    const auto c = cover.create_pi();

    kitty::cube _11 = kitty::cube("11");

    std::vector<kitty::cube> nand_from_offset { _11 }; 
    const auto n1 = cover.create_cover_node( {a, b}, std::make_pair( nand_from_offset, false ) );

    const auto y1 = cover.create_and( n1, c );
    cover.create_po( y1 );

  \endverbatim
 */

class cover_network
{
public:
#pragma region Types and constructors
  static constexpr auto min_fanin_size = 1;
  static constexpr auto max_fanin_size = 32;

  using base_type = cover_network;
  using cover_type = std::pair<std::vector<kitty::cube>, bool>;
  using storage = std::shared_ptr<cover_storage>;
  using node = uint64_t;
  using signal = uint64_t;

  cover_network()
      : _storage( std::make_shared<cover_storage>() ),
        _events( std::make_shared<decltype( _events )::element_type>() )
  {
    _init();
  }

  cover_network( std::shared_ptr<cover_storage> storage )
      : _storage( storage ),
        _events( std::make_shared<decltype( _events )::element_type>() )
  {
    _init();
  }

protected:
  inline void _init()
  {
    uint64_t index;

    std::vector<kitty::cube> cube_dc = { kitty::cube() };

    /* first node reserved for constant 0 */
    index = _storage->data.insert( std::make_pair( cube_dc, false ) );
    cover_storage_node& node_0 = _storage->nodes[0];
    node_0.data[1].h1 = index;
    _storage->hash[node_0] = 0;

    /* reserve the second node for constant 1 */
    _storage->nodes.emplace_back();
    index = _storage->data.insert( std::make_pair( cube_dc, true ) );
    cover_storage_node& node_1 = _storage->nodes[1];
    node_1.data[1].h1 = index;
    _storage->hash[node_1] = 1;

    /* reserve the third node for the identity (inputs)*/
  }
#pragma endregion

#pragma region Primary I / O and constants
public:
  signal get_constant( bool value = false ) const
  {
    return value ? 1 : 0;
  }

  signal create_pi( std::string const& name = std::string() )
  {
    (void)name;

    const auto index_node = _storage->nodes.size();
    std::vector<kitty::cube> cube_I{ kitty::cube( "1" ) };
    const auto index_covers = _storage->data.insert( std::make_pair( cube_I, true ) );

    _storage->nodes.emplace_back();
    cover_storage_node& node_in = _storage->nodes[index_node];
    node_in.data[1].h1 = index_node;
    _storage->hash[node_in] = index_node;
    _storage->inputs.emplace_back( index_node );

    ++_storage->data.num_pis;

    return index_node;
  }

  uint32_t create_po( signal const& f, std::string const& name = std::string() )
  {
    (void)name;

    /* increase ref-count to children */
    _storage->nodes[f].data[0].h1++;

    auto const po_index = static_cast<uint32_t>( _storage->outputs.size() );
    _storage->outputs.emplace_back( f );
    ++_storage->data.num_pos;

    return po_index;
  }
  signal create_ro( std::string const& name = std::string() )
  {
    (void)name;

    auto const index = static_cast<uint32_t>( _storage->nodes.size() );
    _storage->nodes.emplace_back();
    _storage->inputs.emplace_back( index );
    _storage->nodes[index].data[1].h1 = index;
    return index;
  }

  uint32_t create_ri( signal const& f, int8_t reset = 0, std::string const& name = std::string() )
  {
    (void)name;

    /* increase ref-count to children */
    _storage->nodes[f].data[0].h1++;
    auto const ri_index = static_cast<uint32_t>( _storage->outputs.size() );
    _storage->outputs.emplace_back( f );
    _storage->data.latches.emplace_back( reset );
    return ri_index;
  }

  int8_t latch_reset( uint32_t index ) const
  {
    assert( index < _storage->data.latches.size() );
    return _storage->data.latches[index];
  }

  bool is_combinational() const
  {
    return ( static_cast<uint32_t>( _storage->inputs.size() ) == _storage->data.num_pis &&
             static_cast<uint32_t>( _storage->outputs.size() ) == _storage->data.num_pos );
  }

  bool is_constant( node const& n ) const
  {
    return n <= 1;
  }

  bool is_ci( node const& n ) const
  {
    return std::find( _storage->inputs.begin(), _storage->inputs.end(), n ) != _storage->inputs.end();
  }

  bool is_pi( node const& n ) const
  {
    const auto end = _storage->inputs.begin() + _storage->data.num_pis;

    return std::find( _storage->inputs.begin(), end, n ) != end;
  }

  bool is_ro( node const& n ) const
  {
    return std::find( _storage->inputs.begin() + _storage->data.num_pis, _storage->inputs.end(), n ) != _storage->inputs.end();
  }

  bool constant_value( node const& n ) const
  {
    return n == 1;
  }
#pragma endregion

#pragma region Create unary functions
  signal create_buf( signal const& a )
  {
    return a;
  }

  signal create_not( signal const& a )
  {
    std::vector<kitty::cube> _not{ kitty::cube( "0" ) };
    return _create_cover_node( { a }, std::make_pair( _not, true ) );
  }
#pragma endregion

#pragma region Create binary functions
  signal create_and( signal a, signal b )
  {
    std::vector<kitty::cube> _and{ kitty::cube( "11" ) };
    return _create_cover_node( { a, b }, std::make_pair( _and, true ) );
  }

  signal create_nand( signal a, signal b )
  {
    std::vector<kitty::cube> _nand{ kitty::cube( "11" ) };
    return _create_cover_node( { a, b }, std::make_pair( _nand, false ) );
  }

  signal create_or( signal a, signal b )
  {
    std::vector<kitty::cube> _or{ kitty::cube( "00" ) };
    return _create_cover_node( { a, b }, std::make_pair( _or, false ) );
  }

  signal create_nor( signal a, signal b )
  {
    std::vector<kitty::cube> _nor{ kitty::cube( "00" ) };
    return _create_cover_node( { a, b }, std::make_pair( _nor, true ) );
  }

  signal create_lt( signal a, signal b )
  {
    std::vector<kitty::cube> _lt{ kitty::cube( "01" ) };
    return _create_cover_node( { a, b }, std::make_pair( _lt, true ) );
  }

  signal create_le( signal a, signal b )
  {
    std::vector<kitty::cube> _le{ kitty::cube( "10" ) };
    return _create_cover_node( { a, b }, std::make_pair( _le, false ) );
  }

  signal create_gt( signal a, signal b )
  {
    std::vector<kitty::cube> _gt{ kitty::cube( "10" ) };
    return _create_cover_node( { a, b }, std::make_pair( _gt, true ) );
  }

  signal create_ge( signal a, signal b )
  {
    std::vector<kitty::cube> _ge{ kitty::cube( "01" ) };
    return _create_cover_node( { a, b }, std::make_pair( _ge, false ) );
  }

  signal create_xor( signal a, signal b )
  {
    std::vector<kitty::cube> _xor{ kitty::cube( "01" ),
                                   kitty::cube( "10" ) };
    return _create_cover_node( { a, b }, std::make_pair( _xor, true ) );
  }

  signal create_xnor( signal a, signal b )
  {
    std::vector<kitty::cube> _xnor{ kitty::cube( "00" ),
                                    kitty::cube( "11" ) };
    return _create_cover_node( { a, b }, std::make_pair( _xnor, true ) );
  }
#pragma endregion

#pragma region Create ternary functions

  signal create_maj( signal a, signal b, signal c )
  {
    std::vector<kitty::cube> _maj{ kitty::cube( "011" ),
                                   kitty::cube( "101" ),
                                   kitty::cube( "110" ),
                                   kitty::cube( "111" ) };
    return _create_cover_node( { a, b, c }, std::make_pair( _maj, true ) );
  }

  signal create_ite( signal a, signal b, signal c )
  {
    std::vector<kitty::cube> _ite{ kitty::cube( "11-" ),
                                   kitty::cube( "0-1" ) };
    return _create_cover_node( { a, b, c }, std::make_pair( _ite, true ) );
  }

  signal create_xor3( signal a, signal b, signal c )
  {
    std::vector<kitty::cube> _xor3{ kitty::cube( "001" ),
                                    kitty::cube( "010" ),
                                    kitty::cube( "100" ),
                                    kitty::cube( "111" ) };
    return _create_cover_node( { a, b, c }, std::make_pair( _xor3, true ) );
  }
#pragma endregion

#pragma region Create nary functions
  signal create_nary_and( std::vector<signal> const& fs )
  {
    return tree_reduce( fs.begin(), fs.end(), get_constant( true ), [this]( auto const& a, auto const& b ) { return create_and( a, b ); } );
  }

  signal create_nary_or( std::vector<signal> const& fs )
  {
    return tree_reduce( fs.begin(), fs.end(), get_constant( false ), [this]( auto const& a, auto const& b ) { return create_or( a, b ); } );
  }

  signal create_nary_xor( std::vector<signal> const& fs )
  {
    return tree_reduce( fs.begin(), fs.end(), get_constant( false ), [this]( auto const& a, auto const& b ) { return create_xor( a, b ); } );
  }
#pragma endregion

#pragma region Create arbitrary functions
  signal _create_cover_node( std::vector<signal> const& children, cover_type const& new_cover )
  {

    uint64_t literal = _storage->data.insert( new_cover );
    storage::element_type::node_type node;
    std::copy( children.begin(), children.end(), std::back_inserter( node.children ) );
    node.data[1].h1 = literal;

    const auto it = _storage->hash.find( node );
    if ( it != _storage->hash.end() )
    {
      return it->second;
    }

    const auto index = _storage->nodes.size();
    _storage->nodes.emplace_back( node );
    _storage->hash[node] = index;

    /* increase ref-count to children */
    for ( auto c : children )
    {
      _storage->nodes[c].data[0].h1++;
    }

    set_value( index, 0 );

    for ( auto const& fn : _events->on_add )
    {
      ( *fn )( index );
    }

    return index;
  }

  signal create_cover_node( std::vector<signal> const& children, cover_type new_cover )
  {
    if ( children.size() == 0u )
    {
      return get_constant( new_cover.second );
    }

    return _create_cover_node( children, new_cover );
  }

  signal create_cover_node( std::vector<signal> const& children, kitty::dynamic_truth_table const& function )
  {
    if ( children.size() == 0u )
    {
      return get_constant( !kitty::is_const0( function ) );
    }

    cover_type new_cover;
    bool is_sop = ( kitty::count_ones( function ) <= kitty::count_zeros( function ) );
    new_cover.second = is_sop;
    uint32_t mask = 1u;
    for ( uint32_t i{ 1 }; i < children.size(); ++i )
      mask |= mask << 1;
    for ( uint32_t i{ 0 }; i < pow( 2, children.size() ); ++i )
    {
      if ( kitty::get_bit( function, i ) == is_sop )
      {
        auto cb = kitty::cube( i, mask );
        new_cover.first.push_back( cb );
      }
    }

    return _create_cover_node( children, new_cover );
  }

  signal clone_node( cover_network const& other, node const& source, std::vector<signal> const& children )
  {
    assert( !children.empty() );
    cover_type cb = other._storage->data.covers[other._storage->nodes[source].data[1].h1];
    return create_cover_node( children, cb );
  }
#pragma endregion

#pragma region Restructuring
  void substitute_node( node const& old_node, signal const& new_signal )
  {
    /* find all parents from old_node */
    for ( auto i = 0u; i < _storage->nodes.size(); ++i )
    {
      auto& n = _storage->nodes[i];
      for ( auto& child : n.children )
      {
        if ( child == old_node )
        {
          std::vector<signal> old_children( n.children.size() );
          std::transform( n.children.begin(), n.children.end(), old_children.begin(), []( auto c ) { return c.index; } );
          child = new_signal;

          // increment fan-out of new node
          _storage->nodes[new_signal].data[0].h1++;

          for ( auto const& fn : _events->on_modified )
          {
            ( *fn )( i, old_children );
          }
        }
      }
    }

    /* check outputs */
    for ( auto& output : _storage->outputs )
    {
      if ( output == old_node )
      {
        output = new_signal;

        // increment fan-out of new node
        _storage->nodes[new_signal].data[0].h1++;
      }
    }

    // reset fan-out of old node
    _storage->nodes[old_node].data[0].h1 = 0;
  }
#pragma endregion

#pragma region Structural properties
  auto size() const
  {
    return static_cast<uint32_t>( _storage->nodes.size() );
  }

  auto num_cis() const
  {
    return static_cast<uint32_t>( _storage->inputs.size() );
  }

  auto num_cos() const
  {
    return static_cast<uint32_t>( _storage->outputs.size() );
  }

  uint32_t num_latches() const
  {
    return static_cast<uint32_t>( _storage->data.latches.size() );
  }

  auto num_pis() const
  {
    return _storage->data.num_pis;
  }

  auto num_pos() const
  {
    return _storage->data.num_pos;
  }

  auto num_registers() const
  {
    assert( static_cast<uint32_t>( _storage->inputs.size() - _storage->data.num_pis ) == static_cast<uint32_t>( _storage->outputs.size() - _storage->data.num_pos ) );
    return static_cast<uint32_t>( _storage->inputs.size() - _storage->data.num_pis );
  }

  auto num_gates() const
  {
    return static_cast<uint32_t>( _storage->nodes.size() - _storage->inputs.size() - 2 );
  }

  uint32_t fanin_size( node const& n ) const
  {
    return static_cast<uint32_t>( _storage->nodes[n].children.size() );
  }

  uint32_t fanout_size( node const& n ) const
  {
    return _storage->nodes[n].data[0].h1;
  }

  bool is_function( node const& n ) const
  {
    return n > 1 && !is_ci( n );
  }
#pragma endregion

#pragma region Functional properties
  cover_type node_cover( const node& n ) const
  {
    return _storage->data.covers[_storage->nodes[n].data[1].h1];
  }
#pragma endregion

#pragma region Nodes and signals
  node get_node( signal const& f ) const
  {
    return f;
  }

  signal make_signal( node const& n ) const
  {
    return n;
  }

  bool is_complemented( signal const& f ) const
  {
    (void)f;
    return false;
  }

  uint32_t node_to_index( node const& n ) const
  {
    return static_cast<uint32_t>( n );
  }

  node index_to_node( uint32_t index ) const
  {
    return index;
  }

  node ci_at( uint32_t index ) const
  {
    assert( index < _storage->inputs.size() );
    return *( _storage->inputs.begin() + index );
  }

  signal co_at( uint32_t index ) const
  {
    assert( index < _storage->outputs.size() );
    return ( _storage->outputs.begin() + index )->index;
  }

  node pi_at( uint32_t index ) const
  {
    assert( index < _storage->data.num_pis );
    return *( _storage->inputs.begin() + index );
  }

  signal po_at( uint32_t index ) const
  {
    assert( index < _storage->data.num_pos );
    return ( _storage->outputs.begin() + index )->index;
  }

  node ro_at( uint32_t index ) const
  {
    assert( index < _storage->inputs.size() - _storage->data.num_pis );
    return *( _storage->inputs.begin() + _storage->data.num_pis + index );
  }

  signal ri_at( uint32_t index ) const
  {
    assert( index < _storage->outputs.size() - _storage->data.num_pos );
    return ( _storage->outputs.begin() + _storage->data.num_pos + index )->index;
  }

  uint32_t ci_index( node const& n ) const
  {
    assert( _storage->nodes[n].children[0].data == _storage->nodes[n].children[1].data );
    return static_cast<uint32_t>( _storage->nodes[n].children[0].data );
  }

  uint32_t co_index( signal const& s ) const
  {
    uint32_t i = -1;
    foreach_co( [&]( const auto& x, auto index ) {
      if ( x == s )
      {
        i = index;
        return false;
      }
      return true;
    } );
    return i;
  }

  uint32_t pi_index( node const& n ) const
  {
    assert( _storage->nodes[n].children[0].data == _storage->nodes[n].children[1].data );
    return static_cast<uint32_t>( _storage->nodes[n].children[0].data );
  }

  uint32_t po_index( signal const& s ) const
  {
    uint32_t i = -1;
    foreach_po( [&]( const auto& x, auto index ) {
      if ( x == s )
      {
        i = index;
        return false;
      }
      return true;
    } );
    return i;
  }

  uint32_t ro_index( node const& n ) const
  {
    assert( _storage->nodes[n].children[0].data == _storage->nodes[n].children[1].data );
    return static_cast<uint32_t>( _storage->nodes[n].children[0].data - _storage->data.num_pis );
  }

  uint32_t ri_index( signal const& s ) const
  {
    uint32_t i = -1;
    foreach_ri( [&]( const auto& x, auto index ) {
      if ( x == s )
      {
        i = index;
        return false;
      }
      return true;
    } );
    return i;
  }

  signal ro_to_ri( signal const& s ) const
  {
    return ( _storage->outputs.begin() + _storage->data.num_pos + _storage->nodes[s].children[0].data - _storage->data.num_pis )->index;
  }

  node ri_to_ro( signal const& s ) const
  {
    return *( _storage->inputs.begin() + _storage->data.num_pis + ri_index( s ) );
  }
#pragma endregion

#pragma region Node and signal iterators
  template<typename Fn>
  void foreach_node( Fn&& fn ) const
  {
    auto r = range<uint64_t>( _storage->nodes.size() );
    detail::foreach_element( r.begin(), r.end(), fn );
  }

  template<typename Fn>
  void foreach_ci( Fn&& fn ) const
  {
    detail::foreach_element( _storage->inputs.begin(), _storage->inputs.end(), fn );
  }

  template<typename Fn>
  void foreach_co( Fn&& fn ) const
  {
    using IteratorType = decltype( _storage->outputs.begin() );
    detail::foreach_element_transform<IteratorType, uint32_t>(
        _storage->outputs.begin(), _storage->outputs.end(), []( auto o ) { return o.index; },
        fn );
  }

  template<typename Fn>
  void foreach_pi( Fn&& fn ) const
  {
    detail::foreach_element( _storage->inputs.begin(), _storage->inputs.begin() + _storage->data.num_pis, fn );
  }

  template<typename Fn>
  void foreach_po( Fn&& fn ) const
  {
    using IteratorType = decltype( _storage->outputs.begin() );
    detail::foreach_element_transform<IteratorType, uint32_t>(
        _storage->outputs.begin(), _storage->outputs.begin() + _storage->data.num_pos, []( auto o ) { return o.index; },
        fn );
  }

  template<typename Fn>
  void foreach_ro( Fn&& fn ) const
  {
    detail::foreach_element( _storage->inputs.begin() + _storage->data.num_pis, _storage->inputs.end(), fn );
  }

  template<typename Fn>
  void foreach_ri( Fn&& fn ) const
  {
    using IteratorType = decltype( _storage->outputs.begin() );
    detail::foreach_element_transform<IteratorType, uint32_t>(
        _storage->outputs.begin() + _storage->data.num_pos, _storage->outputs.end(), []( auto o ) { return o.index; },
        fn );
  }

  template<typename Fn>
  void foreach_register( Fn&& fn ) const
  {
    static_assert( detail::is_callable_with_index_v<Fn, std::pair<signal, node>, void> ||
                   detail::is_callable_without_index_v<Fn, std::pair<signal, node>, void> ||
                   detail::is_callable_with_index_v<Fn, std::pair<signal, node>, bool> ||
                   detail::is_callable_without_index_v<Fn, std::pair<signal, node>, bool> );

    assert( _storage->inputs.size() - _storage->data.num_pis == _storage->outputs.size() - _storage->data.num_pos );
    auto ro = _storage->inputs.begin() + _storage->data.num_pis;
    auto ri = _storage->outputs.begin() + _storage->data.num_pos;
    if constexpr ( detail::is_callable_without_index_v<Fn, std::pair<signal, node>, bool> )
    {
      while ( ro != _storage->inputs.end() && ri != _storage->outputs.end() )
      {
        if ( !fn( std::make_pair( ( ri++ )->index, ro++ ) ) )
          return;
      }
    }
    else if constexpr ( detail::is_callable_with_index_v<Fn, std::pair<signal, node>, bool> )
    {
      uint32_t index{ 0 };
      while ( ro != _storage->inputs.end() && ri != _storage->outputs.end() )
      {
        if ( !fn( std::make_pair( ( ri++ )->index, ro++ ), index++ ) )
          return;
      }
    }
    else if constexpr ( detail::is_callable_without_index_v<Fn, std::pair<signal, node>, void> )
    {
      while ( ro != _storage->inputs.end() && ri != _storage->outputs.end() )
      {
        fn( std::make_pair( ( ri++ )->index, *ro++ ) );
      }
    }
    else if constexpr ( detail::is_callable_with_index_v<Fn, std::pair<signal, node>, void> )
    {
      uint32_t index{ 0 };
      while ( ro != _storage->inputs.end() && ri != _storage->outputs.end() )
      {
        fn( std::make_pair( ( ri++ )->index, *ro++ ), index++ );
      }
    }
  }

  template<typename Fn>
  void foreach_gate( Fn&& fn ) const
  {
    auto r = range<uint64_t>( 2u, _storage->nodes.size() ); /* start from 2 to avoid constants */
    detail::foreach_element_if(
        r.begin(), r.end(),
        [this]( auto n ) { return !is_ci( n ); },
        fn );
  }

  template<typename Fn>
  void foreach_fanin( node const& n, Fn&& fn ) const
  {
    if ( n == 0 || is_ci( n ) )
      return;

    using IteratorType = decltype( _storage->outputs.begin() );
    detail::foreach_element_transform<IteratorType, uint32_t>(
        _storage->nodes[n].children.begin(), _storage->nodes[n].children.end(), []( auto f ) { return f.index; },
        fn );
  }

#pragma endregion

#pragma region Simulate values
  template<typename Iterator>
  iterates_over_t<Iterator, bool>
  compute( node const& n, Iterator begin, Iterator end ) const
  {
    uint32_t index{ 0 };
    uint32_t mask{ 0 };
    while ( begin != end )
    {
      mask = ( mask << 1 ) | 1u;
      index <<= 1;
      index ^= *begin++ ? 1 : 0;
    }
    auto cb_input = kitty::cube( index, mask );
    cover_type& cubes_cover = _storage->data.covers[_storage->nodes[n].data[1].h1];
    for ( auto cb : cubes_cover.first )
    {
      if ( ( cb._bits & cb._mask ) == ( cb_input._bits & cb._mask ) )
        return ( cubes_cover.second == 1 );
    }

    return ( cubes_cover.second == 0 );
  }

  template<typename Iterator>
  iterates_over_truth_table_t<Iterator>
  compute( node const& n, Iterator begin, Iterator end ) const
  {
    const auto nfanin = _storage->nodes[n].children.size();

    std::vector<typename Iterator::value_type> tts( begin, end );

    assert( nfanin != 0 );
    assert( tts.size() == nfanin );

    /* resulting truth table has the same size as any of the children */
    auto result = tts.front().construct();
    cover_type& cubes_cover = _storage->data.covers[_storage->nodes[n].data[1].h1];
    bool is_found = false;
    for ( uint32_t i = 0u; i < static_cast<uint32_t>( result.num_bits() ); ++i )
    {
      is_found = false;
      uint32_t pattern = 0u;
      uint32_t mask = 0u;
      for ( auto j = 0u; j < nfanin; ++j )
      {
        pattern |= kitty::get_bit( tts[j], i ) << j;
        mask |= 1u << j;
      }
      auto cb_input = kitty::cube( pattern, mask );
      for ( auto cb : cubes_cover.first )
      {
        if ( ( cb._bits & cb._mask ) == ( cb_input._bits & cb._mask ) )
        {
          is_found = true;
          if ( cubes_cover.second == 1 )
          {
            kitty::set_bit( result, i );
          }
          break;
        }
      }
      if ( !is_found && ( cubes_cover.second == 0 ) )
        kitty::set_bit( result, i );
    }

    return result;
  }

#pragma endregion

#pragma region Custom node values
  void clear_values() const
  {
    std::for_each( _storage->nodes.begin(), _storage->nodes.end(), []( auto& n ) { n.data[0].h2 = 0; } );
  }

  uint32_t value( node const& n ) const
  {
    return _storage->nodes[n].data[0].h2;
  }

  void set_value( node const& n, uint32_t v ) const
  {
    _storage->nodes[n].data[0].h2 = v;
  }

  uint32_t incr_value( node const& n ) const
  {
    return static_cast<uint32_t>( _storage->nodes[n].data[0].h2++ );
  }

  uint32_t decr_value( node const& n ) const
  {
    return static_cast<uint32_t>( --_storage->nodes[n].data[0].h2 );
  }
#pragma endregion

#pragma region Visited flags
  void clear_visited() const
  {
    std::for_each( _storage->nodes.begin(), _storage->nodes.end(), []( auto& n ) { n.data[1].h2 = 0; } );
  }

  auto visited( node const& n ) const
  {
    return _storage->nodes[n].data[1].h2;
  }

  void set_visited( node const& n, uint32_t v ) const
  {
    _storage->nodes[n].data[1].h2 = v;
  }

  uint32_t trav_id() const
  {
    return _storage->data.trav_id;
  }

  void incr_trav_id() const
  {
    ++_storage->data.trav_id;
  }
#pragma endregion

#pragma region General methods
  auto& events() const
  {
    return *_events;
  }
#pragma endregion

public:
  std::shared_ptr<cover_storage> _storage;
  std::shared_ptr<network_events<base_type>> _events;
};

} // namespace mockturtle
