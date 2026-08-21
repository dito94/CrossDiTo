export function withBase(path = "") {
  const base = `${import.meta.env.BASE_URL.replace(/\/+$/, "")}/`;
  return `${base}${path.replace(/^\/+/, "")}`;
}
