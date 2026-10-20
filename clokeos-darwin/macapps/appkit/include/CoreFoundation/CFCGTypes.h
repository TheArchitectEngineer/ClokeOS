/* Ersatz für CFCGTypes.h aus Apples SDK, nur beim Bau von Darlings Grafik-Frameworks.
 * Apples CoreFoundation definiert seit macOS 14 CGPoint, CGSize, CGRect, CGVector, CGRectEdge und
 * CGAffineTransform selbst. Darlings CoreGraphics-Header (CGGeometry.h, CGAffineTransform.h)
 * definieren sie ebenfalls, ohne CF_DEFINES_CG_TYPES zu prüfen. Hier bleibt nur CGFloat;
 * die Strukturen kommen aus Darlings Headern, mit demselben Speicherlayout wie bei Apple. */
#if !defined(__COREFOUNDATION_CFCGTYPES__)
#define __COREFOUNDATION_CFCGTYPES__ 1
#include <CoreFoundation/CFBase.h>
#include <float.h>
#include <stdint.h>
#if defined(__has_attribute) && __has_attribute(objc_boxable)
# define CF_BOXABLE __attribute__((objc_boxable))
#else
# define CF_BOXABLE
#endif
#define CGFLOAT_TYPE double
#define CGFLOAT_IS_DOUBLE 1
#define CGFLOAT_MIN DBL_MIN
#define CGFLOAT_MAX DBL_MAX
#define CGFLOAT_EPSILON DBL_EPSILON
typedef CGFLOAT_TYPE CGFloat;
#define CGFLOAT_DEFINED 1
#endif
