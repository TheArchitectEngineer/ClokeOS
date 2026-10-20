# ClokeOS baut QuartzCore ohne Metal (CAMetalLayer.mm braucht Darlings private Metal-Header).
# CALayerContext fragt die Metal-Klasse nur ab; über NSClassFromString bleibt das ohne sie gültig
# (nil, also nie eine Metal-Schicht).
s/\[CAMetalLayerInternal class\]/NSClassFromString(@"CAMetalLayerInternal")/
