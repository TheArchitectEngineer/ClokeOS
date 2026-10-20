# _reverseCompare: liefert bei Gleichheit NSOrderedDescending statt NSOrderedSame; dadurch sind zwei
# gleiche NSNumber verschiedener Klassen (z. B. __NSCFNumber und NSConstantIntegerNumber) ungleich.
/_reverseCompare:/,/^}/{
  /kCFCompareEqualTo:/{
    n
    s/NSOrderedDescending/NSOrderedSame/
  }
}
