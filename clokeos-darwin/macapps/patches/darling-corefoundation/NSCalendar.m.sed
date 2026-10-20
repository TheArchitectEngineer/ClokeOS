# NSCalendarIdentifier*-Konstanten haben bei Darling ihren eigenen Namen als Wert. Bei Apple sind es
# die ICU-/CF-Namen (= kCFCalendarIdentifier*); sonst liefert initWithCalendarIdentifier: nil.
s/(NSCalendarIdentifierGregorian = )@"[^"]*"/\1@"gregorian"/
s/(NSCalendarIdentifierISO8601 = )@"[^"]*"/\1@"iso8601"/
s/(NSCalendarIdentifierBuddhist = )@"[^"]*"/\1@"buddhist"/
s/(NSCalendarIdentifierChinese = )@"[^"]*"/\1@"chinese"/
s/(NSCalendarIdentifierCoptic = )@"[^"]*"/\1@"coptic"/
s/(NSCalendarIdentifierEthiopicAmeteAlem = )@"[^"]*"/\1@"ethiopic-amete-alem"/
s/(NSCalendarIdentifierEthiopicAmeteMihret = )@"[^"]*"/\1@"ethiopic"/
s/(NSCalendarIdentifierHebrew = )@"[^"]*"/\1@"hebrew"/
s/(NSCalendarIdentifierIndian = )@"[^"]*"/\1@"indian"/
s/(NSCalendarIdentifierIslamic = )@"[^"]*"/\1@"islamic"/
s/(NSCalendarIdentifierIslamicCivil = )@"[^"]*"/\1@"islamic-civil"/
s/(NSCalendarIdentifierIslamicTabular = )@"[^"]*"/\1@"islamic-tbla"/
s/(NSCalendarIdentifierIslamicUmmAlQura = )@"[^"]*"/\1@"islamic-umalqura"/
s/(NSCalendarIdentifierJapanese = )@"[^"]*"/\1@"japanese"/
s/(NSCalendarIdentifierPersian = )@"[^"]*"/\1@"persian"/
s/(NSCalendarIdentifierRepublicOfChina = )@"[^"]*"/\1@"roc"/
