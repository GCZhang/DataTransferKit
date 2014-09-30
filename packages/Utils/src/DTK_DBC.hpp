//---------------------------------------------------------------------------//
/*!
 * \file   DTK_DBC.hpp
 * \author Stuart Slattery
 * \brief  Assertions and Design-by-Contract for error handling.
 */
//---------------------------------------------------------------------------//

#ifndef DTK_DBC_HPP
#define DTK_DBC_HPP

#include <stdexcept>
#include <string>

#include "DataTransferKitUtils_config.hpp"

namespace DataTransferKit
{
//---------------------------------------------------------------------------//
/*!
 * \brief Base class for DTK assertions. This structure is heavily based on
 * that in Nemesis developed by Tom Evans. We derive from std::logic_error
 * here as the DBC checks that utilize this class are meant to find errors
 * that can be prevented before runtime.
 */
//---------------------------------------------------------------------------//
class Assertion : public std::logic_error
{
  public:

    /*! 
     * \brief Default constructor.
     *
     * \param msg Error message.
     */
    Assertion( const std::string& msg )
	: std::logic_error( msg )
    { /* ... */ }

    /*! 
     * \brief DBC constructor.
     *
     * \param cond A string containing the assertion condition that failed.
     *
     * \param field A string containing the file name in which the assertion
     * failed. 
     *
     * \param line The line number at which the assertion failed.
     */
    Assertion( const std::string& cond, const std::string& file, 
	       const int line )
	: std::logic_error( generate_output( cond, file, line ) )
    { /* ... */ }

    //! Destructor.
    virtual ~Assertion() throw()
    { /* ... */ }

  private:

    // Build an assertion output from advanced constructor arguments.
    std::string generate_output( const std::string& cond, 
				 const std::string& file, 
				 const int line ) const;
};

//---------------------------------------------------------------------------//
// Throw functions.
//---------------------------------------------------------------------------//
// Throw a DataTransferKit::Assertion.
void throwAssertion( const std::string& cond, const std::string& file,
		     const int line );

// Throw an assertion based on an error code failure.
void errorCodeFailure( const std::string& cond, const std::string& file,
		       const int line, const int error_code );

//---------------------------------------------------------------------------//

} // end namespace DataTransferKit

//---------------------------------------------------------------------------//
// Design-by-Contract macros.
//---------------------------------------------------------------------------//
/*!
  \page DataTransferKit Design-by-Contract.
 
  Design-by-Contract (DBC) functionality is provided to verify function
  preconditions, postconditions, and invariants. These checks are separated
  from the debug build and can be activated for both release and debug
  builds. They can be activated by setting the following in a CMake
  configure:
 
  -D DataTransferKit_ENABLE_DBC:BOOL=ON
 
  By default, DBC is deactivated. Although they will require additional
  computational overhead, these checks provide a mechanism for veryifing
  library input arguments. Note that the bounds-checking functionality used
  within the DataTransferKit is only provided by a debug build.
 
  In addition, DTK_REMEMBER is provided to store values used only for DBC
  checks and no other place in executed code.

  Separate from the DBC build, DTK_INSIST can be used at any time verify a
  conditional. This should be used instead of the standard cassert.

  DTK_CHECK_ERROR_CODE provides DBC support for libraries that return error
  codes with 0 as the value for no errors.
 */

#if HAVE_DTK_DBC

#define DTK_REQUIRE(c) \
    if (!(c)) DataTransferKit::throwAssertion( #c, __FILE__, __LINE__ )
#define DTK_ENSURE(c) \
    if (!(c)) DataTransferKit::throwAssertion( #c, __FILE__, __LINE__ )
#define DTK_CHECK(c) \
    if (!(c)) DataTransferKit::throwAssertion( #c, __FILE__, __LINE__ )
#define DTK_REMEMBER(c) c
#define DTK_CHECK_ERROR_CODE(c) do{int ec=c; if (0!=ec) DataTransferKit::errorCodeFailure( #c, __FILE__, __LINE__, ec );} while(0)

#else

#define DTK_REQUIRE(c)
#define DTK_ENSURE(c)
#define DTK_CHECK(c)
#define DTK_REMEMBER(c)
#define MCLS_CHECK_ERROR_CODE(c) c
#endif


#define DTK_INSIST(c) \
    if (!(c)) DataTransferKit::throwAssertion( #c, __FILE__, __LINE__ )

//---------------------------------------------------------------------------//

#endif // end DTK_DBC_HPP

//---------------------------------------------------------------------------//
// end DTK_DBC.hpp
//---------------------------------------------------------------------------//

