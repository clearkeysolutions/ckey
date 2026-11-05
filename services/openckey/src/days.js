// UTC day helpers. A "day number" is whole days since 1970-01-01 UTC.

const MS_PER_DAY = 86_400_000;
const DAY_RE = /^\d{4}-\d{2}-\d{2}$/;

/** `YYYY-MM-DD` for the UTC day containing `ms`. */
export function utcDay(ms) {
  return new Date(ms).toISOString().slice(0, 10);
}

/** Day number for `ms`. */
export function dayNumberOfMs(ms) {
  return Math.floor(ms / MS_PER_DAY);
}

/** Day number for a `YYYY-MM-DD` string, or NaN when it is not a real calendar day. */
export function dayNumber(day) {
  if (typeof day !== 'string' || !DAY_RE.test(day)) return NaN;
  const t = Date.UTC(Number(day.slice(0, 4)), Number(day.slice(5, 7)) - 1, Number(day.slice(8, 10)));
  if (!Number.isFinite(t) || utcDay(t) !== day) return NaN;
  return t / MS_PER_DAY;
}

/** `YYYY-MM-DD` for a day number. */
export function dayFromNumber(n) {
  return utcDay(n * MS_PER_DAY);
}
