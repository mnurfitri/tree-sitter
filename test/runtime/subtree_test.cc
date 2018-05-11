#include "test_helper.h"
#include "helpers/tree_helpers.h"
#include "helpers/point_helpers.h"
#include "runtime/subtree.h"
#include "runtime/length.h"

void assert_consistent(const Subtree *tree) {
  if (tree->child_count == 0) return;
  AssertThat(tree->children.contents[0]->padding, Equals<Length>(tree->padding));

  Length total_children_size = length_zero();
  for (size_t i = 0; i < tree->children.size; i++) {
    Subtree *child = tree->children.contents[i];
    assert_consistent(child);
    total_children_size = length_add(total_children_size, ts_subtree_total_size(child));
  }

  AssertThat(total_children_size, Equals<Length>(ts_subtree_total_size(tree)));
};

START_TEST

describe("Subtree", []() {
  enum {
    symbol1 = 1,
    symbol2,
    symbol3,
    symbol4,
    symbol5,
    symbol6,
    symbol7,
    symbol8,
    symbol9,
  };

  TSSymbolMetadata metadata_list[30] = {};

  TSLanguage language;
  language.symbol_metadata = metadata_list;

  SubtreePool pool;

  before_each([&]() {
    pool = ts_subtree_pool_new(10);
  });

  after_each([&]() {
    ts_subtree_pool_delete(&pool);
  });

  describe("make_leaf", [&]() {
    it("does not mark the tree as fragile", [&]() {
      Subtree *tree = ts_subtree_make_leaf(&pool, symbol1, {2, {0, 1}}, {5, {0, 4}}, &language);
      AssertThat(tree->fragile_left, IsFalse());
      AssertThat(tree->fragile_right, IsFalse());

      ts_subtree_release(&pool, tree);
    });
  });

  describe("make_error", [&]() {
    it("marks the tree as fragile", [&]() {
      Subtree *error_tree = ts_subtree_make_error(
        &pool,
        length_zero(),
        length_zero(),
        'z',
        &language
      );

      AssertThat(error_tree->fragile_left, IsTrue());
      AssertThat(error_tree->fragile_right, IsTrue());

      ts_subtree_release(&pool, error_tree);
    });
  });

  describe("make_node", [&]() {
    Subtree *tree1, *tree2, *parent1;

    before_each([&]() {
      tree1 = ts_subtree_make_leaf(&pool, symbol1, {2, {0, 1}}, {5, {0, 4}}, &language);
      tree2 = ts_subtree_make_leaf(&pool, symbol2, {1, {0, 1}}, {3, {0, 3}}, &language);

      ts_subtree_retain(tree1);
      ts_subtree_retain(tree2);
      parent1 = ts_subtree_make_node(&pool, symbol3, tree_array({
        tree1,
        tree2,
      }), 0, &language);
    });

    after_each([&]() {
      ts_subtree_release(&pool, tree1);
      ts_subtree_release(&pool, tree2);
      ts_subtree_release(&pool, parent1);
    });

    it("computes its size and padding based on its child nodes", [&]() {
      AssertThat(parent1->size.bytes, Equals<size_t>(
        tree1->size.bytes + tree2->padding.bytes + tree2->size.bytes
      ));
      AssertThat(parent1->padding.bytes, Equals<size_t>(tree1->padding.bytes));
    });

    describe("when the first node is fragile on the left side", [&]() {
      Subtree *parent;

      before_each([&]() {
        tree1->fragile_left = true;
        tree1->extra = true;

        ts_subtree_retain(tree1);
        ts_subtree_retain(tree2);
        parent = ts_subtree_make_node(&pool, symbol3, tree_array({
          tree1,
          tree2,
        }), 0, &language);
      });

      after_each([&]() {
        ts_subtree_release(&pool, parent);
      });

      it("records that it is fragile on the left side", [&]() {
        AssertThat(parent->fragile_left, IsTrue());
      });
    });

    describe("when the last node is fragile on the right side", [&]() {
      Subtree *parent;

      before_each([&]() {
        tree2->fragile_right = true;
        tree2->extra = true;

        ts_subtree_retain(tree1);
        ts_subtree_retain(tree2);
        parent = ts_subtree_make_node(&pool, symbol3, tree_array({
          tree1,
          tree2,
        }), 0, &language);
      });

      after_each([&]() {
        ts_subtree_release(&pool, parent);
      });

      it("records that it is fragile on the right side", [&]() {
        AssertThat(parent->fragile_right, IsTrue());
      });
    });

    describe("when the outer nodes aren't fragile on their outer side", [&]() {
      Subtree *parent;

      before_each([&]() {
        tree1->fragile_right = true;
        tree2->fragile_left = true;

        ts_subtree_retain(tree1);
        ts_subtree_retain(tree2);
        parent = ts_subtree_make_node(&pool, symbol3, tree_array({
          tree1,
          tree2,
        }), 0, &language);
      });

      after_each([&]() {
        ts_subtree_release(&pool, parent);
      });

      it("records that it is not fragile", [&]() {
        AssertThat(parent->fragile_left, IsFalse());
        AssertThat(parent->fragile_right, IsFalse());
      });
    });
  });

  describe("edit", [&]() {
    Subtree *tree;

    before_each([&]() {
      tree = ts_subtree_make_node(&pool, symbol1, tree_array({
        ts_subtree_make_leaf(&pool, symbol2, {2, {0, 2}}, {3, {0, 3}}, &language),
        ts_subtree_make_leaf(&pool, symbol3, {2, {0, 2}}, {3, {0, 3}}, &language),
        ts_subtree_make_leaf(&pool, symbol4, {2, {0, 2}}, {3, {0, 3}}, &language),
      }), 0, &language);

      AssertThat(tree->padding, Equals<Length>({2, {0, 2}}));
      AssertThat(tree->size, Equals<Length>({13, {0, 13}}));
    });

    after_each([&]() {
      ts_subtree_release(&pool, tree);
    });

    it("does not mutate the argument", [&]() {
      TSInputEdit edit;
      edit.start_byte = 1;
      edit.bytes_removed = 0;
      edit.bytes_added = 1;
      edit.start_point = {0, 1};
      edit.extent_removed = {0, 0};
      edit.extent_added = {0, 1};

      ts_subtree_retain(tree);
      Subtree *new_tree = ts_subtree_edit(tree, &edit, &pool);
      assert_consistent(tree);
      assert_consistent(new_tree);

      AssertThat(tree->has_changes, IsFalse());
      AssertThat(tree->padding, Equals<Length>({2, {0, 2}}));
      AssertThat(tree->size, Equals<Length>({13, {0, 13}}));

      AssertThat(tree->children.contents[0]->has_changes, IsFalse());
      AssertThat(tree->children.contents[0]->padding, Equals<Length>({2, {0, 2}}));
      AssertThat(tree->children.contents[0]->size, Equals<Length>({3, {0, 3}}));

      AssertThat(tree->children.contents[1]->has_changes, IsFalse());
      AssertThat(tree->children.contents[1]->padding, Equals<Length>({2, {0, 2}}));
      AssertThat(tree->children.contents[1]->size, Equals<Length>({3, {0, 3}}));

      ts_subtree_release(&pool, new_tree);
    });

    describe("edits within a tree's padding", [&]() {
      it("resizes the padding of the tree and its leftmost descendants", [&]() {
        TSInputEdit edit;
        edit.start_byte = 1;
        edit.bytes_removed = 0;
        edit.bytes_added = 1;
        edit.start_point = {0, 1};
        edit.extent_removed = {0, 0};
        edit.extent_added = {0, 1};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->has_changes, IsTrue());
        AssertThat(tree->padding, Equals<Length>({3, {0, 3}}));
        AssertThat(tree->size, Equals<Length>({13, {0, 13}}));

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
        AssertThat(tree->children.contents[0]->padding, Equals<Length>({3, {0, 3}}));
        AssertThat(tree->children.contents[0]->size, Equals<Length>({3, {0, 3}}));

        AssertThat(tree->children.contents[1]->has_changes, IsFalse());
        AssertThat(tree->children.contents[1]->padding, Equals<Length>({2, {0, 2}}));
        AssertThat(tree->children.contents[1]->size, Equals<Length>({3, {0, 3}}));
      });
    });

    describe("edits that start in a tree's padding but extend into its content", [&]() {
      it("shrinks the content to compensate for the expanded padding", [&]() {
        TSInputEdit edit;
        edit.start_byte = 1;
        edit.bytes_removed = 3;
        edit.bytes_added = 4;
        edit.start_point = {0, 1};
        edit.extent_removed = {0, 3};
        edit.extent_added = {0, 4};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->has_changes, IsTrue());
        AssertThat(tree->padding, Equals<Length>({5, {0, 5}}));
        AssertThat(tree->size, Equals<Length>({11, {0, 11}}));

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
        AssertThat(tree->children.contents[0]->padding, Equals<Length>({5, {0, 5}}));
        AssertThat(tree->children.contents[0]->size, Equals<Length>({1, {0, 1}}));
      });
    });

    describe("insertions at the edge of a tree's padding", [&]() {
      it("expands the tree's padding", [&]() {
        TSInputEdit edit;
        edit.start_byte = 2;
        edit.bytes_removed = 0;
        edit.bytes_added = 2;
        edit.start_point = {0, 2};
        edit.extent_removed = {0, 0};
        edit.extent_added = {0, 2};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->has_changes, IsTrue());
        AssertThat(tree->padding, Equals<Length>({4, {0, 4}}));
        AssertThat(tree->size, Equals<Length>({13, {0, 13}}));

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
        AssertThat(tree->children.contents[0]->padding, Equals<Length>({4, {0, 4}}));
        AssertThat(tree->children.contents[0]->size, Equals<Length>({3, {0, 3}}));

        AssertThat(tree->children.contents[1]->has_changes, IsFalse());
      });
    });

    describe("replacements starting at the edge of a tree's padding", [&]() {
      it("resizes the content and not the padding", [&]() {
        TSInputEdit edit;
        edit.start_byte = 2;
        edit.bytes_removed = 2;
        edit.bytes_added = 5;
        edit.start_point = {0, 2};
        edit.extent_removed = {0, 2};
        edit.extent_added = {0, 5};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->has_changes, IsTrue());
        AssertThat(tree->padding, Equals<Length>({2, {0, 2}}));
        AssertThat(tree->size, Equals<Length>({16, {0, 16}}));

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
        AssertThat(tree->children.contents[0]->padding, Equals<Length>({2, {0, 2}}));
        AssertThat(tree->children.contents[0]->size, Equals<Length>({6, {0, 6}}));

        AssertThat(tree->children.contents[1]->has_changes, IsFalse());
      });
    });

    describe("deletions that span more than one child node", [&]() {
      it("shrinks subsequent child nodes", [&]() {
        TSInputEdit edit;
        edit.start_byte = 1;
        edit.bytes_removed = 10;
        edit.bytes_added = 3;
        edit.start_point = {0, 1};
        edit.extent_removed = {0, 10};
        edit.extent_added = {0, 3};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->has_changes, IsTrue());
        AssertThat(tree->padding, Equals<Length>({4, {0, 4}}));
        AssertThat(tree->size, Equals<Length>({4, {0, 4}}));

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
        AssertThat(tree->children.contents[0]->padding, Equals<Length>({4, {0, 4}}));
        AssertThat(tree->children.contents[0]->size, Equals<Length>({0, {0, 0}}));

        AssertThat(tree->children.contents[1]->has_changes, IsTrue());
        AssertThat(tree->children.contents[1]->padding, Equals<Length>({0, {0, 0}}));
        AssertThat(tree->children.contents[1]->size, Equals<Length>({0, {0, 0}}));

        AssertThat(tree->children.contents[2]->has_changes, IsTrue());
        AssertThat(tree->children.contents[2]->padding, Equals<Length>({1, {0, 1}}));
        AssertThat(tree->children.contents[2]->size, Equals<Length>({3, {0, 3}}));
      });
    });

    describe("edits within a tree's range of scanned bytes", [&]() {
      it("marks preceding trees as changed", [&]() {
        tree->children.contents[0]->bytes_scanned = 7;

        TSInputEdit edit;
        edit.start_byte = 6;
        edit.bytes_removed = 1;
        edit.bytes_added = 1;
        edit.start_point = {0, 6};
        edit.extent_removed = {0, 1};
        edit.extent_added = {0, 1};
        tree = ts_subtree_edit(tree, &edit, &pool);
        assert_consistent(tree);

        AssertThat(tree->children.contents[0]->has_changes, IsTrue());
      });
    });
  });

  describe("eq", [&]() {
    Subtree *leaf;

    before_each([&]() {
      leaf = ts_subtree_make_leaf(&pool, symbol1, {2, {0, 1}}, {5, {0, 4}}, &language);
    });

    after_each([&]() {
      ts_subtree_release(&pool, leaf);
    });

    it("returns true for identical trees", [&]() {
      Subtree *leaf_copy = ts_subtree_make_leaf(&pool, symbol1, {2, {1, 1}}, {5, {1, 4}}, &language);
      AssertThat(ts_subtree_eq(leaf, leaf_copy), IsTrue());

      Subtree *parent = ts_subtree_make_node(&pool, symbol2, tree_array({
        leaf,
        leaf_copy,
      }), 0, &language);
      ts_subtree_retain(leaf);
      ts_subtree_retain(leaf_copy);

      Subtree *parent_copy = ts_subtree_make_node(&pool, symbol2, tree_array({
        leaf,
        leaf_copy,
      }), 0, &language);
      ts_subtree_retain(leaf);
      ts_subtree_retain(leaf_copy);

      AssertThat(ts_subtree_eq(parent, parent_copy), IsTrue());

      ts_subtree_release(&pool, leaf_copy);
      ts_subtree_release(&pool, parent);
      ts_subtree_release(&pool, parent_copy);
    });

    it("returns false for trees with different symbols", [&]() {
      Subtree *different_leaf = ts_subtree_make_leaf(
        &pool,
        leaf->symbol + 1,
        leaf->padding,
        leaf->size,
        &language
      );

      AssertThat(ts_subtree_eq(leaf, different_leaf), IsFalse());
      ts_subtree_release(&pool, different_leaf);
    });

    it("returns false for trees with different options", [&]() {
      Subtree *different_leaf = ts_subtree_make_leaf(&pool, leaf->symbol, leaf->padding, leaf->size, &language);
      different_leaf->visible = !leaf->visible;
      AssertThat(ts_subtree_eq(leaf, different_leaf), IsFalse());
      ts_subtree_release(&pool, different_leaf);
    });

    it("returns false for trees with different paddings or sizes", [&]() {
      Subtree *different_leaf = ts_subtree_make_leaf(&pool, leaf->symbol, {}, leaf->size, &language);
      AssertThat(ts_subtree_eq(leaf, different_leaf), IsFalse());
      ts_subtree_release(&pool, different_leaf);

      different_leaf = ts_subtree_make_leaf(&pool, symbol1, leaf->padding, {}, &language);
      AssertThat(ts_subtree_eq(leaf, different_leaf), IsFalse());
      ts_subtree_release(&pool, different_leaf);
    });

    it("returns false for trees with different children", [&]() {
      Subtree *leaf2 = ts_subtree_make_leaf(&pool, symbol2, {1, {0, 1}}, {3, {0, 3}}, &language);

      Subtree *parent = ts_subtree_make_node(&pool, symbol2, tree_array({
        leaf,
        leaf2,
      }), 0, &language);
      ts_subtree_retain(leaf);
      ts_subtree_retain(leaf2);

      Subtree *different_parent = ts_subtree_make_node(&pool, symbol2, tree_array({
        leaf2,
        leaf,
      }), 0, &language);
      ts_subtree_retain(leaf2);
      ts_subtree_retain(leaf);

      AssertThat(ts_subtree_eq(different_parent, parent), IsFalse());
      AssertThat(ts_subtree_eq(parent, different_parent), IsFalse());

      ts_subtree_release(&pool, leaf2);
      ts_subtree_release(&pool, parent);
      ts_subtree_release(&pool, different_parent);
    });
  });

  describe("last_external_token", [&]() {
    Length padding = {1, {0, 1}};
    Length size = {2, {0, 2}};

    auto make_external = [](Subtree *tree) {
      tree->has_external_tokens = true;
      return tree;
    };

    it("returns the last serialized external token state in the given tree", [&]() {
      Subtree *tree1, *tree2, *tree3, *tree4, *tree5, *tree6, *tree7, *tree8, *tree9;

      tree1 = ts_subtree_make_node(&pool, symbol1,  tree_array({
        (tree2 = ts_subtree_make_node(&pool, symbol2, tree_array({
          (tree3 = make_external(ts_subtree_make_leaf(&pool, symbol3, padding, size, &language))),
          (tree4 = ts_subtree_make_leaf(&pool, symbol4, padding, size, &language)),
          (tree5 = ts_subtree_make_leaf(&pool, symbol5, padding, size, &language)),
        }), 0, &language)),
        (tree6 = ts_subtree_make_node(&pool, symbol6, tree_array({
          (tree7 = ts_subtree_make_node(&pool, symbol7, tree_array({
            (tree8 = ts_subtree_make_leaf(&pool, symbol8, padding, size, &language)),
          }), 0, &language)),
          (tree9 = ts_subtree_make_leaf(&pool, symbol9, padding, size, &language)),
        }), 0, &language)),
      }), 0, &language);

      auto token = ts_subtree_last_external_token(tree1);
      AssertThat(token, Equals(tree3));

      ts_subtree_release(&pool, tree1);
    });
  });
});

END_TEST
