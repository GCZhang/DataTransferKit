//---------------------------------------------------------------------------//
/*
  Copyright (c) 2012, Stuart R. Slattery
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  *: Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  *: Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  *: Neither the name of the University of Wisconsin - Madison nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//---------------------------------------------------------------------------//
/*!
 * \file DTK_SharedDomainMap.hpp
 * \author Stuart R. Slattery
 * \brief Shared domain map definition.
 */
//---------------------------------------------------------------------------//

#ifndef DTK_SHAREDDOMAINMAP_DEF_HPP
#define DTK_SHAREDDOMAINMAP_DEF_HPP

#include <algorithm>
#include <set>

#include <DTK_FieldTools.hpp>
#include <DTK_Exception.hpp>
#include <DTK_Rendezvous.hpp>
#include <DTK_MeshTools.hpp>
#include <DTK_BoundingBox.hpp>

#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_ENull.hpp>
#include <Teuchos_ScalarTraits.hpp>

#include <Tpetra_Import.hpp>
#include <Tpetra_Distributor.hpp>
#include <Tpetra_MultiVector.hpp>

namespace DataTransferKit
{
//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class Mesh, class CoordinateField>
SharedDomainMap<Mesh,CoordinateField>::SharedDomainMap( 
    const RCP_Comm& comm, bool keep_missed_points )
    : d_comm( comm )
    , d_keep_missed_points( keep_missed_points )
{ /* ... */ }

//---------------------------------------------------------------------------//
/*!
 * \brief Destructor.
 */
template<class Mesh, class CoordinateField>
SharedDomainMap<Mesh,CoordinateField>::~SharedDomainMap()
{ /* ... */ }

//---------------------------------------------------------------------------//
/*!
 * \brief Generate the shared domain map.
 */
template<class Mesh, class CoordinateField>
void SharedDomainMap<Mesh,CoordinateField>::setup( 
    const RCP_MeshManager& source_mesh_manager, 
    const RCP_FieldManager& target_coord_manager )
{
    // Get the global bounding box for the mesh.
    BoundingBox source_box = source_mesh_manager->globalBoundingBox();

    // Get the global bounding box for the coordinate field.
    BoundingBox target_box = 
	FieldTools<CoordinateField>::coordGlobalBoundingBox(
	    target_coord_manager->field(), d_comm );

    // Intersect the boxes to get the shared domain bounding box.
    BoundingBox shared_domain_box;
    bool has_intersect = 
    	BoundingBox::intersectBoxes( source_box, target_box, shared_domain_box );
    if ( !has_intersect )
    {
    	throw MeshException( 
    	    "Source and target geometry domains do not intersect." );
    }
    
    // Build a rendezvous decomposition with the source mesh.
    Rendezvous<Mesh> rendezvous( d_comm, shared_domain_box );
    rendezvous.build( source_mesh_manager );

    // Get the target points that are in the box in which the rendezvous
    // decomposition was generated.
    Teuchos::Array<short int> targets_in_box;
    getTargetPointsInBox( rendezvous.getBox(), target_coord_manager->field(), 
			  targets_in_box );

    // Compute a unique global ordinal for each point in the coordinate field.
    Teuchos::Array<GlobalOrdinal> point_ordinals;
    computePointOrdinals( target_coord_manager->field(), point_ordinals );

    // Build the data import map from the point global ordinals.
    Teuchos::ArrayView<const GlobalOrdinal> import_ordinal_view =
	point_ordinals();
    d_import_map = Tpetra::createNonContigMap<GlobalOrdinal>(
	import_ordinal_view, d_comm );
    testPostcondition( d_import_map != Teuchos::null,
		       "Error creating data import map." );

    // Determine the rendezvous destination proc of each point in the
    // coordinate field that is in the rendezvous decomposition box.
    std::size_t coord_dim = CFT::dim( target_coord_manager->field() );
    CoordOrdinal num_coords = CFT::size( target_coord_manager->field() );
    Teuchos::ArrayRCP<double> coords_view;
    if ( num_coords == 0 )
    {
	coords_view = Teuchos::ArrayRCP<double>( 0, 0.0 );
    }
    else
    {
	coords_view = FieldTools<CoordinateField>::nonConstView( 
	    target_coord_manager->field() );
    }
    Teuchos::Array<int> rendezvous_procs = 
	rendezvous.procsContainingPoints( coords_view );

    // Via an inverse communication operation, move the global point ordinals
    // that are in the rendezvous decomposition box to the rendezvous
    // decomposition.
    Tpetra::Distributor point_distributor( d_comm );
    GlobalOrdinal num_rendezvous_points = 
	point_distributor.createFromSends( rendezvous_procs() );
    Teuchos::Array<GlobalOrdinal> rendezvous_points( num_rendezvous_points );
    point_distributor.doPostsAndWaits( 
	import_ordinal_view, 1, rendezvous_points() );

    // Setup Target-to-rendezvous communication.
    Teuchos::ArrayView<const GlobalOrdinal> rendezvous_points_view =
	rendezvous_points();
    RCP_TpetraMap rendezvous_import_map = 
	Tpetra::createNonContigMap<GlobalOrdinal>(
	    rendezvous_points_view, d_comm );
    Tpetra::Export<GlobalOrdinal> 
	point_exporter( d_import_map, rendezvous_import_map );

    // Move the target coordinates to the rendezvous decomposition.
    GlobalOrdinal num_points = num_coords / coord_dim;
    Teuchos::RCP< Tpetra::MultiVector<double,GlobalOrdinal> > 
	target_coords =	createMultiVectorFromView( d_import_map, coords_view,
						   num_points, coord_dim );
    Tpetra::MultiVector<double,GlobalOrdinal> rendezvous_coords( 
	rendezvous_import_map, coord_dim );
    rendezvous_coords.doExport( *target_coords, point_exporter, 
				Tpetra::INSERT );

    // Search the rendezvous decomposition with the target points to get the
    // source elements that contain them.
    Teuchos::Array<GlobalOrdinal> rendezvous_elements =
	rendezvous.elementsContainingPoints( 
	    rendezvous_coords.get1dViewNonConst() );

    // Build a unique list of rendezvous elements.
    Teuchos::Array<GlobalOrdinal> rendezvous_element_set =
	rendezvous_elements;
    std::sort( rendezvous_element_set.begin(), rendezvous_element_set.end() );
    typename Teuchos::Array<GlobalOrdinal>::iterator unique_bound =
	std::unique( rendezvous_element_set.begin(), 
		     rendezvous_element_set.end() );
    rendezvous_element_set.resize( 
	unique_bound - rendezvous_element_set.begin() );

    // Setup source-to-rendezvous communication.
    Teuchos::ArrayView<const GlobalOrdinal> rendezvous_elem_set_view =
	rendezvous_element_set();
    RCP_TpetraMap rendezvous_element_map = 
	Tpetra::createNonContigMap<GlobalOrdinal>( 
	    rendezvous_elem_set_view, d_comm );

    // The block strategy means that we have to make a temporary copy of the
    // element handles so that we can make a Tpetra::Map.
    Teuchos::Array<GlobalOrdinal> 
	mesh_elements( source_mesh_manager->localNumElements() );
    GlobalOrdinal start = 0;
    BlockIterator block_iterator;
    for ( block_iterator = source_mesh_manager->blocksBegin();
	  block_iterator != source_mesh_manager->blocksEnd();
	  ++block_iterator )
    {
	Teuchos::ArrayRCP<const GlobalOrdinal> mesh_element_arcp =
	    MeshTools<Mesh>::elementsView( *block_iterator );
	std::copy( mesh_element_arcp.begin(), mesh_element_arcp.end(),
		   mesh_elements.begin() + start );
	start += mesh_element_arcp.size();
    }

    Teuchos::ArrayView<const GlobalOrdinal> mesh_element_view = 
	mesh_elements();
    RCP_TpetraMap mesh_element_map = 
	Tpetra::createNonContigMap<GlobalOrdinal>( 
	    mesh_element_view, d_comm );
    mesh_elements.clear();

    Tpetra::Import<GlobalOrdinal> source_importer( 
	mesh_element_map, rendezvous_element_map );

    // Elements send their source decomposition proc to the rendezvous
    // decomposition.
    Tpetra::MultiVector<int,GlobalOrdinal> 
	source_procs( mesh_element_map, 1 );
    source_procs.putScalar( d_comm->getRank() );
    Tpetra::MultiVector<int,GlobalOrdinal> 
	rendezvous_source_procs( rendezvous_element_map, 1 );
    rendezvous_source_procs.doImport( source_procs, source_importer, 
				      Tpetra::INSERT );

    // Set the destination procs for the rendezvous-to-source communication.
    Teuchos::ArrayRCP<const int> rendezvous_source_procs_view =
	rendezvous_source_procs.get1dView();
    GlobalOrdinal num_rendezvous_elements = rendezvous_elements.size();
    GlobalOrdinal dest_proc_idx;
    Teuchos::Array<int> 
	target_destinations( num_rendezvous_elements );
    for ( GlobalOrdinal n = 0; n < num_rendezvous_elements; ++n )
    {
	dest_proc_idx = std::distance( 
	    rendezvous_element_set.begin(),
	    std::find( rendezvous_element_set.begin(),
		       rendezvous_element_set.end(),
		       rendezvous_elements[n] ) );
	target_destinations[n] = rendezvous_source_procs_view[ dest_proc_idx ];
    }
    rendezvous_element_set.clear();

    // Send the rendezvous elements to the source decomposition via inverse
    // communication. 
    Teuchos::ArrayView<const GlobalOrdinal> rendezvous_elements_view =
	rendezvous_elements();
    Tpetra::Distributor source_distributor( d_comm );
    GlobalOrdinal num_source_elements = 
	source_distributor.createFromSends( target_destinations() );
    d_source_elements.resize( num_source_elements );
    source_distributor.doPostsAndWaits( rendezvous_elements_view, 1,
					d_source_elements() );

    // Send the rendezvous point coordinates to the source decomposition via
    // inverse communication. 
    Teuchos::ArrayView<const double> rendezvous_coords_view =
	rendezvous_coords.get1dView()();
    d_target_coords.resize( num_source_elements*coord_dim );
    source_distributor.doPostsAndWaits( rendezvous_coords_view, coord_dim,
					d_target_coords() );

    // Send the rendezvous point global ordinals to the source decomposition
    // via inverse communication.
    Teuchos::Array<GlobalOrdinal> source_points( num_source_elements );
    source_distributor.doPostsAndWaits( rendezvous_points_view, 1,
					source_points() );

    // Build the data export map from the coordinate ordinals as well as
    // populate the source element/target coordinate pairings.
    Teuchos::ArrayView<const GlobalOrdinal> source_points_view =
	source_points();
    d_export_map = Tpetra::createNonContigMap<GlobalOrdinal>(
	source_points_view, d_comm );
    testPostcondition( d_export_map != Teuchos::null,
		       "Error creating data export map." );

    // Build the data importer.
    d_data_export = Teuchos::rcp( new Tpetra::Export<GlobalOrdinal>(
				      d_export_map, d_import_map ) );
    testPostcondition( d_data_export != Teuchos::null,
		       "Error creating data importer." );
}

//---------------------------------------------------------------------------//
/*!
 * \brief If keep_missed_points is true, return the local indices of the
 *  points provided by target_coord_manager that were not mapped. An exception
 *  will be thrown if keep_missed_points is false.
*/
template<class Mesh, class CoordinateField>
Teuchos::ArrayView<const typename 
		   SharedDomainMap<Mesh,CoordinateField>::CoordOrdinal> 
SharedDomainMap<Mesh,CoordinateField>::getMissedTargetPoints() const
{
    testPrecondition( d_keep_missed_points, 
      "Cannot get missing target points; keep_missed_points = false" );
    
    return d_missed_points();
}

//---------------------------------------------------------------------------//
/*!
 * \brief Apply the shared domain map for a valid source field evaluator and
 * target data space to the target points that were mapped.
 */
template<class Mesh, class CoordinateField>
template<class SourceField, class TargetField>
void SharedDomainMap<Mesh,CoordinateField>::apply( 
    const Teuchos::RCP< FieldEvaluator<Mesh,SourceField> >& source_evaluator,
    Teuchos::RCP< FieldManager<TargetField> >& target_space_manager )
{
    typedef FieldTraits<SourceField> SFT;
    typedef FieldTraits<TargetField> TFT;

    testPrecondition( 
	FieldTools<TargetField>::dimSize( target_space_manager->field() )
	== (typename TFT::size_type) d_import_map->getNodeNumElements(),
	"Number of target field elements != Number of coordinate field elements" );

    SourceField function_evaluations = 
	source_evaluator->evaluate( Teuchos::arcpFromArray( d_source_elements ),
				    Teuchos::arcpFromArray( d_target_coords ) );
    testPrecondition( SFT::dim( function_evaluations ) == 
		      TFT::dim( target_space_manager->field() ),
		      "Source field dimension != target field dimension." );

    Teuchos::ArrayRCP<typename SFT::value_type> source_field_view;
    if ( SFT::size( function_evaluations ) == 0 )
    {
	source_field_view = 
	    Teuchos::ArrayRCP<typename SFT::value_type>( 0, 0.0 );
    }
    else
    {
	source_field_view = 
	    FieldTools<SourceField>::nonConstView( function_evaluations );
    }

    GlobalOrdinal source_size = SFT::size( function_evaluations ) /
				SFT::dim( function_evaluations );

    Teuchos::RCP< Tpetra::MultiVector<typename SFT::value_type,
				      GlobalOrdinal> > source_vector = 
	createMultiVectorFromView( d_export_map, 
				   source_field_view,
				   source_size,
				   SFT::dim( function_evaluations ) );

    Teuchos::ArrayRCP<typename TFT::value_type> target_field_view;
    if ( TFT::size( target_space_manager->field() ) == 0 )
    {
	target_field_view = 
	    Teuchos::ArrayRCP<typename TFT::value_type>( 0, 0.0 );
    }
    else
    {
	target_field_view = FieldTools<TargetField>::nonConstView( 
	    target_space_manager->field() );
    }
    
    GlobalOrdinal target_size = TFT::size( target_space_manager->field() ) /
				TFT::dim( target_space_manager->field() );

    Teuchos::RCP< Tpetra::MultiVector<typename TFT::value_type,
				      GlobalOrdinal> > target_vector =	
	createMultiVectorFromView( d_import_map, 
				   target_field_view,
				   target_size,
				   TFT::dim( target_space_manager->field() ) );

    target_vector->doExport( *source_vector, *d_data_export, Tpetra::INSERT );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the target points that are in the rendezvous decomposition box.
 */
template<class Mesh, class CoordinateField>
void SharedDomainMap<Mesh,CoordinateField>::getTargetPointsInBox(
    const BoundingBox& box, const CoordinateField& target_coords,
    Teuchos::Array<short int>& points_in_box )
{
    Teuchos::ArrayRCP<const double> target_coords_view =
	FieldTools<CoordinateField>::view( target_coords );
    CoordOrdinal dim_size = 
	FieldTools<CoordinateField>::dimSize( target_coords );
    points_in_box.resize( dim_size );
    std::size_t field_dim = CFT::dim( target_coords );
    Teuchos::Array<double> target_point( field_dim );
    for ( CoordOrdinal n = 0; n < dim_size; ++n )
    {
	for ( std::size_t d = 0; d < field_dim; ++d )
	{
	    target_point[d] = target_coords_view[ dim_size*d + n ];
	}

	points_in_box[n] = box.pointInBox( target_point );

	// If we're keeping track of the points not being mapped, add this
	// point's local index to the list.
	if ( d_keep_missed_points && points_in_box[n] == 0 )
	{
	    d_missed_points.push_back(n);
	}
    }
}

//---------------------------------------------------------------------------//
/*!
 * \brief Compute globally unique ordinals for the target points
 */
template<class Mesh, class CoordinateField>
void SharedDomainMap<Mesh,CoordinateField>::computePointOrdinals(
    const CoordinateField& target_coords,
    Teuchos::Array<GlobalOrdinal>& ordinals )
{
    int comm_rank = d_comm->getRank();
    int point_dim = CFT::dim( target_coords );
    GlobalOrdinal local_size = 
	std::distance( CFT::begin( target_coords ),
		       CFT::end( target_coords ) ) / point_dim;

    GlobalOrdinal global_size;
    Teuchos::reduceAll<int,GlobalOrdinal>( *d_comm,
					   Teuchos::REDUCE_MAX,
					   1,
					   &local_size,
					   &global_size );

    ordinals.resize( local_size );
    for ( GlobalOrdinal n = 0; n < local_size; ++n )
    {
	ordinals[n] = comm_rank*global_size + n;
    }
}

//---------------------------------------------------------------------------//

} // end namespace DataTransferKit

#endif // end DTK_SHAREDDOMAINMAP_DEF_HPP

//---------------------------------------------------------------------------//
// end DTK_SharedDomainMap_def.hpp
//---------------------------------------------------------------------------//
