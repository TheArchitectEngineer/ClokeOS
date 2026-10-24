import CoreImage
import Foundation
import ImageIO
import UniformTypeIdentifiers

// Zuschnitt auf 16:10 aus der Mitte, 2560 x 1600; dunkle Fassung: abdunkeln, Richtung Violett, Vignette
let args = CommandLine.arguments
let src = CIImage(contentsOf: URL(fileURLWithPath: args[1]))!
let w = src.extent.width, h = src.extent.height
let ch = w / 1.6, y0 = (h - ch) / 2
var img = src.cropped(to: CGRect(x: 0, y: y0, width: w, height: ch))
img = img.transformed(by: CGAffineTransform(translationX: 0, y: -y0))
let s = 2560 / w
img = img.transformed(by: CGAffineTransform(scaleX: s, y: s))
let rect = CGRect(x: 0, y: 0, width: 2560, height: 1600)
func write(_ im: CIImage, _ path: String) {
  let ctx = CIContext()
  let cs = CGColorSpace(name: CGColorSpace.sRGB)!
  let cg = ctx.createCGImage(im, from: rect, format: .RGBA8, colorSpace: cs)!
  let d = CGImageDestinationCreateWithURL(URL(fileURLWithPath: path) as CFURL, UTType.jpeg.identifier as CFString, 1, nil)!
  CGImageDestinationAddImage(d, cg, [kCGImageDestinationLossyCompressionQuality: 0.9] as CFDictionary)
  CGImageDestinationFinalize(d)
}
write(img, args[2])
let plum = CIImage(color: CIColor(red: 0.36, green: 0.21, blue: 0.46)).cropped(to: rect)
var dark = img.applyingFilter("CIMultiplyBlendMode", parameters: [kCIInputBackgroundImageKey: plum])
dark = dark.applyingFilter("CIColorControls", parameters: [kCIInputSaturationKey: 0.85, kCIInputBrightnessKey: 0.02, kCIInputContrastKey: 1.05])
let vig = dark.applyingFilter("CIVignette", parameters: [kCIInputIntensityKey: 0.7, kCIInputRadiusKey: 2.8])
write(vig, args[3])
