module ElaborationResult = {
  include OptUtil;

  type t = option((HTyp.t, Kind.t, Delta.t));
};
open ElaborationResult.Syntax;

let rec get_prod_elements: UHTyp.skel => list(UHTyp.skel) =
  fun
  | BinOp(_, Prod, skel1, skel2) =>
    get_prod_elements(skel1) @ get_prod_elements(skel2)
  | skel => [skel];

let rec syn: (Contexts.t, Delta.t, UHTyp.t) => ElaborationResult.t =
  (ctx, delta) =>
    fun
    | OpSeq(skel, seq) => syn_skel(ctx, delta, skel, seq)
and syn_skel = (ctx, delta, skel, seq) =>
  switch (skel) {
  | Placeholder(n) => seq |> Seq.nth_operand(n) |> syn_operand(ctx, delta)
  | BinOp(_, Prod, _, _) =>
    /* TElabSBinOp */
    let* rs =
      skel
      |> get_prod_elements
      |> List.map(skel => ana_skel(ctx, delta, Kind.Type, skel, seq))
      |> OptUtil.sequence;
    let (tys, ks, ds) = ListUtil.unzip3(rs);
    let+ () = ks |> List.for_all(k => k == Kind.Type) |> OptUtil.of_bool;
    let delta =
      ds |> List.fold_left((d1, d2) => Delta.union(d1, d2), Delta.empty);
    (HTyp.Prod(tys), Kind.Type, delta);
  | BinOp(_, op, skel1, skel2) =>
    /* TElabSBinOp */
    let* (ty1, k1, d1) = ana_skel(ctx, delta, Kind.Type, skel1, seq);
    let* (ty2, k2, d2) = ana_skel(ctx, delta, Kind.Type, skel2, seq);
    switch (k1, k2) {
    | (Kind.Type, Kind.Type) =>
      let ty =
        switch (op) {
        | Arrow => HTyp.Arrow(ty1, ty2)
        | Sum => HTyp.Sum(ty1, ty2)
        | Prod => failwith("Impossible, Prod is matched first")
        };
      Some((ty, Kind.Type, Delta.union(d1, d2)));
    | (_, _) => None
    };
  }
and syn_operand = (ctx, delta, operand) => {
  let const = (c: HTyp.t) => {
    /* TElabSConst */
    Some((c, Kind.Type, delta));
  };

  switch (operand) {
  | Hole =>
    /* TElabSHole */
    // TODO: Thread u properly
    Some((
      HTyp.Hole,
      Kind.KHole,
      Delta.add(
        0,
        Delta.V(Delta.TypeHole, Kind.KHole, Contexts.tyvars(ctx)),
        delta,
      ),
    ))
  // TODO: NEHole case
  | TyVar(NotInVarHole, t) =>
    /* TElabSVar */
    let+ idx = TyVarCtx.index_of(Contexts.tyvars(ctx), t);
    let (_, k) = TyVarCtx.tyvar_with_idx(Contexts.tyvars(ctx), idx);
    (HTyp.TyVar(idx, TyId.to_string(t)), k, delta);
  | TyVar(InVarHole(_, u), t) =>
    /* TElabSUVar */
    // TODO: id(\Phi) in TyVarHole
    Some((
      HTyp.TyVarHole(u, TyId.to_string(t)),
      Kind.KHole,
      Delta.add(
        u,
        Delta.V(Delta.TypeHole, Kind.KHole, Contexts.tyvars(ctx)),
        delta,
      ),
    ))
  | Unit => const(Prod([]))
  | Int => const(Int)
  | Float => const(Float)
  | Bool => const(Bool)
  | Parenthesized(opseq) => syn(ctx, delta, opseq)
  | List(opseq) =>
    /* TElabSList */
    let* (ty, k, delta) = ana(ctx, delta, opseq, Kind.Type);
    let+ () = k == Kind.Type |> OptUtil.of_bool;
    (HTyp.List(ty), Kind.Type, delta);
  };
}

and ana: (Contexts.t, Delta.t, UHTyp.t, Kind.t) => ElaborationResult.t =
  (ctx, delta, opseq, kind) =>
    switch (opseq) {
    | OpSeq(skel, seq) => ana_skel(ctx, delta, kind, skel, seq)
    }
and ana_skel = (ctx, delta, kind, skel, seq) =>
  switch (skel) {
  | Placeholder(n) =>
    seq |> Seq.nth_operand(n) |> ana_operand(ctx, delta, kind)
  | BinOp(_, _, _, _) =>
    /* TElabASubsume */
    let* (ty, k', delta) = syn_skel(ctx, delta, skel, seq);
    let+ () = Kind.consistent(kind, k') |> OptUtil.of_bool;
    (ty, k', delta);
  }
and ana_operand = (ctx, delta, kind, operand) => {
  switch (operand) {
  | UHTyp.Hole =>
    /* TElabAHole */
    // TODO: Thread u properly
    Some((
      Hole,
      Kind.KHole,
      Delta.add(
        0,
        Delta.V(Delta.TypeHole, Kind.KHole, Contexts.tyvars(ctx)),
        delta,
      ),
    ))
  // TODO: Is this the only NEHole case? Even though it's the syn UVar case?
  | TyVar(InVarHole(_, u), t) =>
    // TODO: id(\Phi) in TyVarHole
    Some((
      TyVarHole(u, TyId.to_string(t)),
      Kind.KHole,
      Delta.add(
        u,
        Delta.V(Delta.TypeHole, Kind.KHole, Contexts.tyvars(ctx)),
        delta,
      ),
    ))
  | Parenthesized(opseq) => ana(ctx, delta, opseq, kind)
  | TyVar(NotInVarHole, _)
  | Unit
  | Int
  | Float
  | Bool
  | List(_) =>
    /* TElabASubsume */
    let* (ty, k', delta) = syn_operand(ctx, delta, operand);
    let+ () = Kind.consistent(kind, k') |> OptUtil.of_bool;
    (ty, k', delta);
  };
};