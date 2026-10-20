/* ClokeOS: schlankes ApplicationServices.h für den Bau von Darlings Grafik-Frameworks, ähnlich
 * Darlings eigenem. Apples Fassung bringt QuickDraw mit (QD/QD.h, auch über HIShape.h und
 * Icons.h). Dessen "struct Picture" stößt sich mit dem Picture-Typ aus den X11-Headern im
 * AppKit-X11-Backend, und Darling benutzt QuickDraw nirgends. */
#ifndef __APPLICATIONSERVICES__
#define __APPLICATIONSERVICES__
#include <Availability.h>
#include <CoreServices/CoreServices.h>
#include <ATS/ATS.h>
#include <ColorSync/ColorSync.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <HIServices/InternetConfig.h>
#include <HIServices/Processes.h>
#include <HIServices/Pasteboard.h>
#include <HIServices/TranslationServices.h>
#include <HIServices/Accessibility.h>
#include <HIServices/UniversalAccess.h>
#include <ImageIO/ImageIO.h>
#endif
