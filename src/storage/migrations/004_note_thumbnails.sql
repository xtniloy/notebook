-- Schema version 4: images in notes (task 3.7).
--
-- A note's list thumbnail is the thumbnail of its first image, named here by
-- the attachment's uuid. The note service sets it on every save, from the
-- order of the images in the body, so the list shows the first one without
-- reading bodies (TC-STO-014). NULL: no image.
--
-- Attachments are read per note (opening a note, cleaning up on save), so
-- they get an index on note_id.

ALTER TABLE notes ADD COLUMN thumbnail_uuid TEXT;
CREATE INDEX idx_attachments_note ON attachments(note_id);
