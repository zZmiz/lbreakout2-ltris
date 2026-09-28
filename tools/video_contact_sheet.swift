// Read-only video inspection. Usage: input.mov output.png step-seconds
import Foundation
import AVFoundation
import CoreGraphics
import CoreText
import ImageIO
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count == 4, let step = Double(args[3]), step > 0 else { exit(2) }
let asset = AVURLAsset(url: URL(fileURLWithPath: args[1]))
let duration = CMTimeGetSeconds(asset.duration)
let generator = AVAssetImageGenerator(asset: asset)
generator.appliesPreferredTrackTransform = true
generator.maximumSize = CGSize(width: 640, height: 360)
generator.requestedTimeToleranceBefore = .zero
generator.requestedTimeToleranceAfter = .zero
let count = Int(ceil(duration / step)), columns = 4, tileW = 480, tileH = 300
let rows = (count + columns - 1) / columns
let context = CGContext(data: nil, width: tileW*columns, height: tileH*rows,
    bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
context.setFillColor(CGColor(gray: 0.12, alpha: 1))
context.fill(CGRect(x: 0, y: 0, width: tileW*columns, height: tileH*rows))
for i in 0..<count {
    let seconds = Double(i)*step
    let frame = try generator.copyCGImage(at: CMTime(seconds: seconds, preferredTimescale: 600), actualTime: nil)
    let x = (i%columns)*tileW, y = (rows - 1 - i/columns)*tileH
    context.draw(frame, in: CGRect(x: x, y: y+25, width: tileW, height: 270))
    let label = NSAttributedString(string: String(format: "%.2f s", seconds), attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): CTFontCreateWithName("Menlo" as CFString, 18, nil),
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): CGColor(gray: 1, alpha: 1)])
    context.textPosition = CGPoint(x: x+8, y: y+5)
    CTLineDraw(CTLineCreateWithAttributedString(label), context)
}
let output = CGImageDestinationCreateWithURL(URL(fileURLWithPath: args[2]) as CFURL,
    UTType.png.identifier as CFString, 1, nil)!
CGImageDestinationAddImage(output, context.makeImage()!, nil)
guard CGImageDestinationFinalize(output) else { exit(1) }
print("Extracted \(count) frames from \(duration) seconds: \(args[2])")
