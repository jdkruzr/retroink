let books = [];
let shelves = [];
let shelfModalMode = "create";
let shelfModalTargetId = null;

function escapeHtml(s) {
  const div = document.createElement("div");
  div.textContent = s == null ? "" : String(s);
  return div.innerHTML;
}

function showMessage(text, isError) {
  const el = document.getElementById("message");
  el.textContent = text;
  el.className = "message " + (isError ? "error" : "success");
  el.style.display = "block";
  clearTimeout(showMessage._t);
  showMessage._t = setTimeout(() => {
    el.style.display = "none";
  }, 4000);
}

async function loadLibrary() {
  try {
    const [booksRes, shelvesRes] = await Promise.all([fetch("/api/library/books"), fetch("/api/library/shelves")]);
    if (!booksRes.ok || !shelvesRes.ok) throw new Error("Request failed");
    books = await booksRes.json();
    shelves = await shelvesRes.json();
    renderSmartBoard();
    renderCustomBoard();
    document.getElementById("library-summary").textContent =
      books.length + (books.length === 1 ? " book" : " books");
  } catch (e) {
    document.getElementById("smartBoard").innerHTML = '<p class="shelf-empty">Could not load the library.</p>';
    document.getElementById("customBoard").innerHTML = "";
    showMessage("Could not load the library.", true);
  }
}

function switchShelfTab(tab) {
  document.getElementById("smartTabBtn").classList.toggle("active", tab === "smart");
  document.getElementById("customTabBtn").classList.toggle("active", tab === "custom");
  document.getElementById("smartPanel").classList.toggle("active", tab === "smart");
  document.getElementById("customPanel").classList.toggle("active", tab === "custom");
}

function bookCardHtml(book, draggable) {
  return (
    '<div class="book-card' +
    (draggable ? "" : " smart") +
    '" data-fingerprint="' +
    escapeHtml(book.fingerprint) +
    '"' +
    (draggable ? ' data-draggable="1"' : "") +
    '><div class="book-card-title">' +
    escapeHtml(book.title || book.path) +
    '</div><div class="book-card-author">' +
    escapeHtml(book.author || "") +
    "</div></div>"
  );
}

function renderSmartBoard() {
  const board = document.getElementById("smartBoard");
  const groups = [
    { title: "To Read", filter: (b) => b.toRead },
    { title: "Reading", filter: (b) => b.reading },
    { title: "Finished", filter: (b) => b.finished },
    { title: "Favorites", filter: (b) => b.favorite },
  ];
  board.innerHTML = groups
    .map((g) => {
      const items = books.filter(g.filter);
      return (
        '<div class="shelf-row"><div class="shelf-row-header"><span class="shelf-row-title">' +
        g.title +
        '</span><span class="shelf-row-count">' +
        items.length +
        '</span></div><div class="shelf-row-books">' +
        (items.length ? items.map((b) => bookCardHtml(b, false)).join("") : '<p class="shelf-empty">No books</p>') +
        "</div></div>"
      );
    })
    .join("");
}

function renderCustomBoard() {
  const board = document.getElementById("customBoard");
  const unassigned = books.filter((b) => !b.shelfId);
  const rows = [{ id: 0, name: "Unassigned", system: true }].concat(shelves);
  board.innerHTML = rows
    .map((shelf) => {
      const items = shelf.id === 0 ? unassigned : books.filter((b) => b.shelfId === shelf.id);
      const actions = shelf.system
        ? ""
        : '<span class="shelf-row-actions">' +
          '<button onclick="openRenameShelfPrompt(' +
          shelf.id +
          ')" title="Rename">&#9998;</button>' +
          '<button onclick="confirmDeleteShelf(' +
          shelf.id +
          ')" title="Delete">&times;</button></span>';
      return (
        '<div class="shelf-row" data-shelf-id="' +
        shelf.id +
        '"><div class="shelf-row-header"><span class="shelf-row-title">' +
        escapeHtml(shelf.name) +
        '</span><span class="shelf-row-count">' +
        items.length +
        "</span>" +
        actions +
        '</div><div class="shelf-row-books" data-shelf-id="' +
        shelf.id +
        '">' +
        (items.length
          ? items.map((b) => bookCardHtml(b, true)).join("")
          : '<p class="shelf-empty">Drop books here</p>') +
        "</div></div>"
      );
    })
    .join("");
  attachDragHandlers();
}

// Hand-rolled on Pointer Events rather than the native HTML5 drag-and-drop
// API: this portal is also used from phones/tablets (see the existing
// responsive breakpoints throughout web/pages/*.css), and native
// dragstart/dragover/drop does not fire on touch browsers without a
// polyfill. Pointer Events work the same way for mouse and touch.
function attachDragHandlers() {
  document.querySelectorAll('#customBoard .book-card[data-draggable="1"]').forEach((card) => {
    card.addEventListener("pointerdown", onCardPointerDown);
  });
}

function onCardPointerDown(downEvent) {
  // Without this, dragging the ghost across other shelf rows/text starts a
  // native text-selection gesture (the pointer is still "held down" as far
  // as the browser's own selection handling is concerned, regardless of
  // setPointerCapture below). Suppressing it here, once, is enough - no
  // need to keep calling it on every pointermove.
  downEvent.preventDefault();
  const card = downEvent.currentTarget;
  const fingerprint = card.dataset.fingerprint;
  const startX = downEvent.clientX;
  const startY = downEvent.clientY;
  let dragging = false;
  let ghost = null;

  function clearHighlights() {
    document.querySelectorAll("#customBoard .shelf-row").forEach((row) => row.classList.remove("drag-over"));
  }

  function rowUnderPoint(x, y) {
    if (ghost) ghost.style.display = "none";
    const under = document.elementFromPoint(x, y);
    if (ghost) ghost.style.display = "";
    return under && under.closest ? under.closest("#customBoard .shelf-row") : null;
  }

  function onMove(moveEvent) {
    const dx = moveEvent.clientX - startX;
    const dy = moveEvent.clientY - startY;
    if (!dragging && Math.hypot(dx, dy) > 6) {
      dragging = true;
      card.classList.add("dragging");
      ghost = card.cloneNode(true);
      ghost.classList.add("drag-ghost");
      ghost.style.width = card.offsetWidth + "px";
      document.body.appendChild(ghost);
    }
    if (!dragging) return;
    ghost.style.left = moveEvent.clientX + "px";
    ghost.style.top = moveEvent.clientY + "px";
    clearHighlights();
    const row = rowUnderPoint(moveEvent.clientX, moveEvent.clientY);
    if (row) row.classList.add("drag-over");
  }

  function onUp(upEvent) {
    card.removeEventListener("pointermove", onMove);
    card.removeEventListener("pointerup", onUp);
    card.removeEventListener("pointercancel", onUp);
    try {
      card.releasePointerCapture(downEvent.pointerId);
    } catch (e) {
      /* already released */
    }
    clearHighlights();
    card.classList.remove("dragging");
    const wasDragging = dragging;
    const row = wasDragging ? rowUnderPoint(upEvent.clientX, upEvent.clientY) : null;
    if (ghost) {
      ghost.remove();
      ghost = null;
    }
    if (!wasDragging || !row) return;
    const shelfId = parseInt(row.dataset.shelfId, 10);
    assignBookToShelf(fingerprint, shelfId);
  }

  card.setPointerCapture(downEvent.pointerId);
  card.addEventListener("pointermove", onMove);
  card.addEventListener("pointerup", onUp);
  card.addEventListener("pointercancel", onUp);
}

async function assignBookToShelf(fingerprint, shelfId) {
  const book = books.find((b) => String(b.fingerprint) === String(fingerprint));
  const previousShelfId = book ? book.shelfId : 0;
  if (book) book.shelfId = shelfId; // optimistic
  renderCustomBoard();
  try {
    const res = await fetch("/api/library/assign", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ fingerprint: String(fingerprint), shelfId: shelfId }),
    });
    if (!res.ok) throw new Error(await res.text());
  } catch (e) {
    if (book) book.shelfId = previousShelfId; // roll back
    renderCustomBoard();
    showMessage("Could not move that book.", true);
  }
}

function openNewShelfPrompt() {
  shelfModalMode = "create";
  shelfModalTargetId = null;
  document.getElementById("shelfModalTitle").textContent = "New Shelf";
  document.getElementById("shelfNameInput").value = "";
  document.getElementById("shelfModal").classList.add("open");
  document.getElementById("shelfNameInput").focus();
}

function openRenameShelfPrompt(id) {
  const shelf = shelves.find((s) => s.id === id);
  if (!shelf) return;
  shelfModalMode = "rename";
  shelfModalTargetId = id;
  document.getElementById("shelfModalTitle").textContent = "Rename Shelf";
  document.getElementById("shelfNameInput").value = shelf.name;
  document.getElementById("shelfModal").classList.add("open");
  document.getElementById("shelfNameInput").focus();
}

function closeShelfModal() {
  document.getElementById("shelfModal").classList.remove("open");
}

async function submitShelfModal() {
  const name = document.getElementById("shelfNameInput").value.trim();
  if (!name) return;
  const isCreate = shelfModalMode === "create";
  const url = isCreate ? "/api/library/shelves/create" : "/api/library/shelves/rename";
  const body = isCreate ? { name: name } : { id: shelfModalTargetId, name: name };
  try {
    const res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    if (!res.ok) throw new Error(await res.text());
    closeShelfModal();
    await loadLibrary();
    showMessage(isCreate ? "Shelf created." : "Shelf renamed.", false);
  } catch (e) {
    showMessage("Could not save the shelf.", true);
  }
}

async function confirmDeleteShelf(id) {
  const shelf = shelves.find((s) => s.id === id);
  if (!shelf) return;
  if (!confirm('Delete "' + shelf.name + '"? Its books go back to Unassigned.')) return;
  try {
    const res = await fetch("/api/library/shelves/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ id: id }),
    });
    if (!res.ok) throw new Error(await res.text());
    await loadLibrary();
    showMessage("Shelf deleted.", false);
  } catch (e) {
    showMessage("Could not delete the shelf.", true);
  }
}

loadLibrary();
